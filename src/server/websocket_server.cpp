#include "server/websocket_server.h"
#include "network/protocol.h"
#include "network/message_handler.h"
#include "utils/logger.h"

#include <App.h>  // uWebSockets main header

#include <string>
#include <sstream>
#include <random>
#include <algorithm>

namespace server {

// Generate a simple random ID (Phase 1 only — JWT-based in Phase 2)
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
    : cfg_(cfg) {}

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
        logger::warn("max rooms reached (" + std::to_string(cfg_.max_rooms) + "), rejecting room creation");
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

void WebSocketServer::run() {
    // uWS is single-threaded — everything runs on this event loop.
    // That means no locks needed for rooms_ or player_sockets_.

    uWS::App()
        .ws<PerSocketData>("/ws/*", {
            // Settings
            .compression = uWS::DISABLED,
            .maxPayloadLength = 16 * 1024,  // 16 KB — plenty for JSON messages
            .idleTimeout = 120,             // 2 min without pong → disconnect
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
                    room_id = url.substr(4);  // strip "/ws/"
                }

                std::string token = params.count("token") ? params["token"] : "";
                std::string name = params.count("name") ? params["name"] : "Player";

                // Validate room_id is present
                if (room_id.empty()) {
                    res->writeStatus("400 Bad Request")
                       ->end("Missing 'room' query parameter");
                    return;
                }

                // Phase 1: generate a player ID (Phase 2 will extract from JWT)
                std::string player_id = generate_id();

                // Check room availability before upgrading
                auto* room = get_or_create_room(room_id);
                if (!room) {
                    res->writeStatus("503 Service Unavailable")
                       ->end("Server at max room capacity");
                    return;
                }
                if (room->is_full()) {
                    res->writeStatus("403 Forbidden")
                       ->end("Room is full");
                    return;
                }
                if (room->state() != game::RoomState::WAITING) {
                    res->writeStatus("403 Forbidden")
                       ->end("Room is already in game");
                    return;
                }

                // Accept the upgrade — fill in per-socket data
                res->template upgrade<PerSocketData>(
                    {
                        .player_id = player_id,
                        .player_name = name,
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

                // Store socket reference
                player_sockets_[data->player_id] = ws;

                auto* room = get_room(data->room_id);
                if (!room) {
                    ws->send(network::make_error(500, "Room disappeared").dump(),
                             uWS::OpCode::TEXT);
                    ws->close();
                    return;
                }

                // Set up the broadcast function so the room can send
                // messages through player sockets
                room->set_broadcast_fn(
                    [this](const std::string& pid, const std::string& message) {
                        auto it = player_sockets_.find(pid);
                        if (it != player_sockets_.end()) {
                            auto* target = static_cast<uWS::WebSocket<false, true, PerSocketData>*>(it->second);
                            target->send(message, uWS::OpCode::TEXT);
                        }
                    }
                );

                // Add player to room
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
                              network::make_connected(data->player_id, 0));

                // Notify others that a new player joined
                room->broadcast_except(data->player_id,
                    network::make_player_joined(data->player_id, data->player_name));

                // Send full lobby state to everyone
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
            .close = [this](auto* ws, int code, std::string_view reason) {
                auto* data = ws->getUserData();
                logger::info("ws close | player=" + data->player_id
                             + " room=" + data->room_id
                             + " code=" + std::to_string(code));

                // Remove socket reference
                player_sockets_.erase(data->player_id);

                auto* room = get_room(data->room_id);
                if (room) {
                    room->remove_player(data->player_id);

                    // Notify remaining players
                    room->broadcast(network::make_player_left(data->player_id));

                    // Send updated lobby state
                    if (!room->is_empty()) {
                        room->broadcast(room->lobby_state());
                    }
                }

                // Periodically clean up finished rooms
                cleanup_empty_rooms();
            }
        })

        // ── Health check endpoint (for Docker / Traefik) ─────
        .get("/health", [](auto* res, auto* /*req*/) {
            res->writeHeader("Content-Type", "application/json")
               ->end("{\"status\":\"ok\"}");
        })

        // ── Server info endpoint ─────────────────────────────
        .get("/info", [this](auto* res, auto* /*req*/) {
            int total_players = 0;
            for (const auto& [_, room] : rooms_) {
                total_players += room->player_count();
            }
            nlohmann::json info = {
                {"rooms_active", rooms_.size()},
                {"players_online", total_players}
            };
            res->writeHeader("Content-Type", "application/json")
               ->end(info.dump());
        })

        .listen(cfg_.port, [this](auto* listen_socket) {
            if (listen_socket) {
                logger::info("game server listening on port " + std::to_string(cfg_.port));
                logger::info("max_rooms=" + std::to_string(cfg_.max_rooms)
                             + " max_players_per_room=" + std::to_string(cfg_.max_players_per_room));
            } else {
                logger::error("failed to listen on port " + std::to_string(cfg_.port));
            }
        })

        .run();
}

} // namespace server
