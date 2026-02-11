#include "server/websocket_server.h"
#include "server/jwt.h"
#include "network/protocol.h"
#include "network/message_handler.h"
#include "utils/logger.h"

#include <App.h>  // uWebSockets main header

// uSockets timer API for the game loop
extern "C" {
    struct us_timer_t;
    struct us_loop_t;
    struct us_timer_t *us_create_timer(struct us_loop_t *loop, int fallthrough, unsigned int ext_size);
    void us_timer_set(struct us_timer_t *timer, void (*cb)(struct us_timer_t *), int ms, int repeat_ms);
    void us_timer_close(struct us_timer_t *timer);
    void *us_timer_ext(struct us_timer_t *timer);
}

#include <string>
#include <sstream>
#include <random>
#include <algorithm>
#include <cstring>

namespace server {

// Fallback ID generator (used if JWT validation is disabled)
static std::string generate_id(int len = 8) {
    static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, sizeof(chars) - 2);

    std::string id;
    id.reserve(len);
    for (int i = 0; i < len; ++i) {
        id += chars[dist(rng)];
    }
    return id;
}

WebSocketServer::WebSocketServer(const config::ServerConfig& cfg)
    : cfg_(cfg) {
    tick_dt_ = 1.0f / static_cast<float>(cfg.tick_rate);

    // Connect to Redis and fetch JWT secret
    if (redis_.connect(cfg.redis_addr, cfg.redis_port, cfg.redis_password)) {
        auto secret = redis_.get("jwt:secret");
        if (secret) {
            jwt_secret_ = *secret;
            logger::info("JWT secret loaded from Redis (" + std::to_string(jwt_secret_.size()) + " bytes)");
        } else {
            logger::warn("jwt:secret not found in Redis — JWT validation disabled");
        }
    } else {
        logger::warn("Redis not available — JWT validation disabled, running in dev mode");
    }
}

std::unordered_map<std::string, std::string> WebSocketServer::parse_query(std::string_view url) {
    std::unordered_map<std::string, std::string> params;

    auto qpos = url.find('?');
    if (qpos == std::string_view::npos) return params;

    auto query = url.substr(qpos + 1);
    std::string key, value;
    bool in_key = true;

    for (char c : query) {
        if (c == '=') {
            in_key = false;
        } else if (c == '&') {
            if (!key.empty()) params[key] = value;
            key.clear();
            value.clear();
            in_key = true;
        } else {
            if (in_key) key += c;
            else value += c;
        }
    }
    if (!key.empty()) params[key] = value;

    return params;
}

game::Room* WebSocketServer::get_or_create_room(const std::string& room_id) {
    auto it = rooms_.find(room_id);
    if (it != rooms_.end()) {
        return it->second.get();
    }

    if (static_cast<int>(rooms_.size()) >= cfg_.max_rooms) {
        logger::warn("max rooms reached (" + std::to_string(cfg_.max_rooms) + "), rejecting");
        return nullptr;
    }

    auto room = std::make_unique<game::Room>(room_id, cfg_.max_players_per_room);
    auto* ptr = room.get();
    rooms_.emplace(room_id, std::move(room));
    logger::info("created room " + room_id);
    return ptr;
}

game::Room* WebSocketServer::get_room(const std::string& room_id) {
    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) return nullptr;
    return it->second.get();
}

void WebSocketServer::cleanup_empty_rooms() {
    for (auto it = rooms_.begin(); it != rooms_.end();) {
        if (it->second->is_empty() && it->second->state() == game::RoomState::FINISHED) {
            logger::info("cleaning up empty room " + it->first);
            it = rooms_.erase(it);
        } else {
            ++it;
        }
    }
}

void WebSocketServer::setup_room_broadcast(game::Room* room) {
    room->set_broadcast_fn(
        [this](const std::string& pid, const std::string& message) {
            auto it = player_sockets_.find(pid);
            if (it != player_sockets_.end()) {
                auto* ws = static_cast<uWS::WebSocket<false, true, PerSocketData>*>(it->second);
                ws->send(message, uWS::OpCode::TEXT);
            }
        }
    );
}

void WebSocketServer::tick() {
    tick_count_++;

    for (auto& [id, room] : rooms_) {
        if (room->state() == game::RoomState::PLAYING) {
            room->update(tick_dt_);
        }
    }
}

void WebSocketServer::run() {
    uWS::App()
        .ws<PerSocketData>("/ws/*", {
            .compression = uWS::DISABLED,
            .maxPayloadLength = 16 * 1024,
            .idleTimeout = 120,
            .maxBackpressure = 64 * 1024,

            // ── Upgrade (HTTP → WS handshake) ────────────────
            .upgrade = [this](auto* res, auto* req, auto* context) {
                auto url = std::string(req->getUrl());
                auto query_str = std::string(req->getQuery());
                auto full_url = url + "?" + query_str;
                auto params = parse_query(full_url);

                // Extract room code from path: /ws/{roomCode}
                std::string room_id;
                if (url.rfind("/ws/", 0) == 0 && url.size() > 4) {
                    room_id = url.substr(4);
                }

                std::string token = params.count("token") ? params["token"] : "";

                // Validate room_id
                if (room_id.empty()) {
                    res->writeStatus("400 Bad Request")
                       ->end("Missing room code in path");
                    return;
                }

                // ── JWT validation ──────────────────────────
                std::string player_id;
                std::string player_name = "Player";

                if (!jwt_secret_.empty() && !token.empty()) {
                    auto payload = auth::validate_jwt(token, jwt_secret_);
                    if (!payload) {
                        res->writeStatus("401 Unauthorized")
                           ->end("Invalid or expired token");
                        return;
                    }
                    player_id = payload->sub;
                    player_name = payload->username;
                    logger::debug("JWT validated for player " + player_id + " (" + player_name + ")");
                } else {
                    // Dev mode fallback: generate random ID
                    player_id = generate_id();
                    logger::debug("no JWT — generated player_id " + player_id);
                }

                // Check room availability
                auto* room = get_or_create_room(room_id);
                if (!room) {
                    res->writeStatus("503 Service Unavailable")
                       ->end("Server at max room capacity");
                    return;
                }

                // Check if player is already in this room (reconnect scenario)
                if (room->has_player(player_id)) {
                    // Remove old connection, new one will replace it
                    auto sock_it = player_sockets_.find(player_id);
                    if (sock_it != player_sockets_.end()) {
                        auto* old_ws = static_cast<uWS::WebSocket<false, true, PerSocketData>*>(sock_it->second);
                        old_ws->getUserData()->player_id = "";  // prevent double-remove
                        old_ws->close();
                    }
                    room->remove_player(player_id);
                }

                if (room->is_full()) {
                    res->writeStatus("403 Forbidden")
                       ->end("Room is full");
                    return;
                }
                if (room->state() == game::RoomState::FINISHED) {
                    res->writeStatus("403 Forbidden")
                       ->end("Room is finished");
                    return;
                }

                res->template upgrade<PerSocketData>(
                    {
                        .player_id = player_id,
                        .player_name = player_name,
                        .room_id = room_id
                    },
                    req->getHeader("sec-websocket-key"),
                    req->getHeader("sec-websocket-protocol"),
                    req->getHeader("sec-websocket-extensions"),
                    context
                );
            },

            // ── Connection opened ────────────────────────────
            .open = [this](auto* ws) {
                auto* data = ws->getUserData();
                logger::info("ws open | player=" + data->player_id
                             + " name=" + data->player_name
                             + " room=" + data->room_id);

                player_sockets_[data->player_id] = ws;

                auto* room = get_room(data->room_id);
                if (!room) {
                    ws->send(network::make_error(500, "Room disappeared").dump(),
                             uWS::OpCode::TEXT);
                    ws->close();
                    return;
                }

                setup_room_broadcast(room);

                game::Player player;
                player.id = data->player_id;
                player.name = data->player_name;
                player.display_name = data->player_name;

                if (!room->add_player(player)) {
                    ws->send(network::make_error(403, "Could not join room").dump(),
                             uWS::OpCode::TEXT);
                    ws->close();
                    return;
                }

                // Send "connected" to the new player
                room->send_to(data->player_id,
                              network::make_connected(data->player_id, data->player_name, room->current_tick()));

                // Notify others
                room->broadcast_except(data->player_id,
                    network::make_player_joined(data->player_id, data->player_name));

                // Send lobby state to everyone
                room->broadcast(room->lobby_state());
            },

            // ── Message received ─────────────────────────────
            .message = [this](auto* ws, std::string_view message, uWS::OpCode /*opCode*/) {
                auto* data = ws->getUserData();

                auto parsed = network::parse_message(message);
                if (!parsed) {
                    ws->send(network::make_error(400, "Invalid JSON").dump(),
                             uWS::OpCode::TEXT);
                    return;
                }

                auto* room = get_room(data->room_id);
                if (!room) {
                    ws->send(network::make_error(404, "Room not found").dump(),
                             uWS::OpCode::TEXT);
                    return;
                }

                network::handle_message(*room, data->player_id, *parsed);
            },

            // ── Connection closed ────────────────────────────
            .close = [this](auto* ws, int code, std::string_view /*reason*/) {
                auto* data = ws->getUserData();

                // Skip if already cleaned up (reconnect scenario)
                if (data->player_id.empty()) return;

                logger::info("ws close | player=" + data->player_id
                             + " room=" + data->room_id
                             + " code=" + std::to_string(code));

                player_sockets_.erase(data->player_id);

                auto* room = get_room(data->room_id);
                if (room) {
                    room->remove_player(data->player_id);
                    room->broadcast(network::make_player_left(data->player_id));

                    if (!room->is_empty()) {
                        room->broadcast(room->lobby_state());
                    }
                }

                cleanup_empty_rooms();
            }
        })

        // ── Health check ─────────────────────────────────
        .get("/health", [](auto* res, auto* /*req*/) {
            res->writeHeader("Content-Type", "application/json")
               ->end("{\"status\":\"ok\"}");
        })

        // ── Server info ──────────────────────────────────
        .get("/info", [this](auto* res, auto* /*req*/) {
            int total_players = 0;
            int playing_rooms = 0;
            for (const auto& [_, room] : rooms_) {
                total_players += room->player_count();
                if (room->state() == game::RoomState::PLAYING) playing_rooms++;
            }
            nlohmann::json info = {
                {"rooms_active", rooms_.size()},
                {"rooms_playing", playing_rooms},
                {"players_online", total_players},
                {"tick", tick_count_}
            };
            res->writeHeader("Content-Type", "application/json")
               ->end(info.dump());
        })

        .listen(cfg_.port, [this](auto* listen_socket) {
            if (listen_socket) {
                logger::info("game server listening on port " + std::to_string(cfg_.port));
                logger::info("tick_rate=" + std::to_string(cfg_.tick_rate)
                             + " tick_dt=" + std::to_string(tick_dt_) + "s"
                             + " jwt=" + (jwt_secret_.empty() ? "disabled" : "enabled"));

                // ── Start game loop timer ────────────────
                int tick_ms = static_cast<int>(tick_dt_ * 1000.0f);
                auto* timer = us_create_timer(
                    (struct us_loop_t*) uWS::Loop::get(), 0, sizeof(WebSocketServer*));
                WebSocketServer* self = this;
                memcpy(us_timer_ext(timer), &self, sizeof(WebSocketServer*));
                us_timer_set(timer, [](struct us_timer_t* t) {
                    WebSocketServer* srv;
                    memcpy(&srv, us_timer_ext(t), sizeof(WebSocketServer*));
                    srv->tick();
                }, tick_ms, tick_ms);

                logger::info("game loop started at " + std::to_string(cfg_.tick_rate) + " ticks/s");
            } else {
                logger::error("failed to listen on port " + std::to_string(cfg_.port));
            }
        })

        .run();
}

} // namespace server
