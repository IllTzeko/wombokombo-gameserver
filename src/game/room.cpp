#include "game/room.h"
#include "utils/logger.h"

namespace game {

Room::Room(std::string id, int max_players)
    : id_(std::move(id)), max_players_(max_players) {}

// ── Player management ───────────────────────────────

bool Room::add_player(const Player& player) {
    if (has_player(player.id)) return false;

    Player p = player;

    // Check if this is a reconnecting player during gameplay
    auto disc_it = disconnected_players_.find(player.id);
    if (disc_it != disconnected_players_.end()) {
        // Restore their state from before disconnect
        p = disc_it->second;
        p.name = player.name;  // Update name in case it changed
        p.display_name = player.display_name;
        disconnected_players_.erase(disc_it);
        logger::info("player " + p.id + " (" + p.name + ") reconnected to room " + id_
                     + " at (" + std::to_string((int)p.x) + "," + std::to_string((int)p.y) + ")");
    } else {
        // New player
        if (is_full()) return false;
        if (state_ == RoomState::FINISHED) return false;

        if (state_ == RoomState::PLAYING) {
            int idx = next_spawn_ % 4;
            p.spawn(spawn_positions_[idx][0], spawn_positions_[idx][1]);
            next_spawn_++;
        }

        logger::info("player " + p.id + " (" + p.name + ") joined room " + id_);
    }

    players_.emplace(p.id, p);

    // Room is no longer empty
    empty_since_.reset();

    return true;
}

void Room::remove_player(const std::string& player_id) {
    auto it = players_.find(player_id);
    if (it == players_.end()) return;

    // If game is in progress, save player state for reconnection
    if (state_ == RoomState::PLAYING) {
        disconnected_players_[player_id] = it->second;
        logger::info("player " + player_id + " disconnected from room " + id_
                     + " (saved for reconnect, grace=" + std::to_string(GRACE_SECONDS) + "s)");
    } else {
        logger::info("player " + player_id + " left room " + id_);
    }

    players_.erase(it);

    if (players_.empty()) {
        if (state_ == RoomState::PLAYING && !disconnected_players_.empty()) {
            // Start grace period — keep room alive for reconnection
            empty_since_ = Clock::now();
            logger::info("room " + id_ + " has no connected players, grace period started");
        } else if (state_ == RoomState::WAITING) {
            state_ = RoomState::FINISHED;
            logger::info("room " + id_ + " is now empty, marked finished");
        }
    }
}

bool Room::has_player(const std::string& player_id) const {
    return players_.count(player_id) > 0;
}

std::optional<Player> Room::get_player(const std::string& player_id) const {
    auto it = players_.find(player_id);
    if (it == players_.end()) return std::nullopt;
    return it->second;
}

bool Room::is_full() const {
    return static_cast<int>(players_.size()) >= max_players_;
}

bool Room::is_empty() const {
    return players_.empty();
}

int Room::player_count() const {
    return static_cast<int>(players_.size());
}

bool Room::should_cleanup() const {
    if (state_ == RoomState::FINISHED && players_.empty()) return true;

    // Check grace period expiry
    if (empty_since_) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            Clock::now() - *empty_since_).count();
        if (elapsed >= GRACE_SECONDS) {
            return true;
        }
    }

    return false;
}

// ── Lobby ───────────────────────────────────────────

void Room::set_player_ready(const std::string& player_id, bool ready) {
    auto it = players_.find(player_id);
    if (it == players_.end()) return;

    it->second.ready = ready;

    broadcast({
        {"type", "player_ready_state"},
        {"player_id", player_id},
        {"ready", ready}
    });

    logger::debug("player " + player_id + " ready=" + (ready ? "true" : "false")
                  + " in room " + id_);

    // Auto-start when all players are ready (min 2)
    if (all_ready() && state_ == RoomState::WAITING) {
        logger::info("all players ready in room " + id_ + " — starting game");
        start_game();
    }
}

bool Room::all_ready() const {
    if (players_.empty()) return false;
    if (players_.size() < 2) return false;
    for (const auto& [_, p] : players_) {
        if (!p.ready) return false;
    }
    return true;
}

// ── Chat ────────────────────────────────────────────

void Room::handle_chat(const std::string& sender_id, const std::string& message) {
    auto player = get_player(sender_id);
    if (!player) return;

    broadcast({
        {"type", "chat_message"},
        {"player_id", sender_id},
        {"player_name", player->name},
        {"message", message}
    });
}

// ── Gameplay ────────────────────────────────────────

void Room::start_game() {
    if (state_ != RoomState::WAITING) return;

    state_ = RoomState::PLAYING;
    tick_ = 0;
    next_spawn_ = 0;

    // Spawn all players at different positions
    for (auto& [pid, player] : players_) {
        int idx = next_spawn_ % 4;
        player.spawn(spawn_positions_[idx][0], spawn_positions_[idx][1]);
        next_spawn_++;
    }

    // Build spawn points array for the client
    nlohmann::json spawn_points = nlohmann::json::array();
    for (const auto& [pid, player] : players_) {
        spawn_points.push_back({
            {"player_id", pid},
            {"x", player.x},
            {"y", player.y}
        });
    }

    // Notify all clients
    broadcast({
        {"type", "game_start"},
        {"round", 1},
        {"map_data", {
            {"width", physics::MAP_WIDTH},
            {"height", physics::MAP_HEIGHT},
            {"ground_y", physics::GROUND_Y}
        }},
        {"spawn_points", spawn_points}
    });

    logger::info("game started in room " + id_ + " with " + std::to_string(player_count()) + " players");
}

void Room::update(float dt) {
    if (state_ != RoomState::PLAYING) return;

    // Check grace period expiry
    if (empty_since_) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            Clock::now() - *empty_since_).count();
        if (elapsed >= GRACE_SECONDS) {
            logger::info("room " + id_ + " grace period expired, marking finished");
            state_ = RoomState::FINISHED;
            disconnected_players_.clear();
            return;
        }
    }

    // Don't tick if no players are connected
    if (players_.empty()) return;

    tick_++;

    // Process pending inputs for each player
    for (auto& [pid, player] : players_) {
        player.process_input(dt);
    }

    // Broadcast game state every tick to connected players
    broadcast(game_state());
}

void Room::queue_input(const std::string& player_id,
                       int tick,
                       const std::vector<std::string>& actions) {
    auto it = players_.find(player_id);
    if (it == players_.end()) return;

    it->second.pending_actions = actions;
    it->second.last_input_tick = tick;
}

// ── Broadcasting ────────────────────────────────────

void Room::set_broadcast_fn(BroadcastFn fn) {
    broadcast_fn_ = std::move(fn);
}

void Room::broadcast(const nlohmann::json& msg) {
    if (!broadcast_fn_) return;
    std::string serialized = msg.dump();
    for (const auto& [pid, _] : players_) {
        broadcast_fn_(pid, serialized);
    }
}

void Room::broadcast_except(const std::string& exclude_id, const nlohmann::json& msg) {
    if (!broadcast_fn_) return;
    std::string serialized = msg.dump();
    for (const auto& [pid, _] : players_) {
        if (pid != exclude_id) {
            broadcast_fn_(pid, serialized);
        }
    }
}

void Room::send_to(const std::string& player_id, const nlohmann::json& msg) {
    if (!broadcast_fn_) return;
    broadcast_fn_(player_id, msg.dump());
}

// ── State snapshots ─────────────────────────────────

nlohmann::json Room::lobby_state() const {
    nlohmann::json players_arr = nlohmann::json::array();
    for (const auto& [_, p] : players_) {
        players_arr.push_back(p.to_lobby_json());
    }
    return {
        {"type", "lobby_state"},
        {"room_id", id_},
        {"state", room_state_str(state_)},
        {"max_players", max_players_},
        {"players", players_arr}
    };
}

nlohmann::json Room::game_state() const {
    nlohmann::json players_arr = nlohmann::json::array();
    for (const auto& [_, p] : players_) {
        players_arr.push_back(p.to_game_json());
    }

    return {
        {"type", "game_state"},
        {"tick", tick_},
        {"time_left", 60.0f},    // Phase 3: actual round timer
        {"round", 1},             // Phase 3: round tracking
        {"players", players_arr},
        {"enemies", nlohmann::json::array()},
        {"items", nlohmann::json::array()}
    };
}

} // namespace game
