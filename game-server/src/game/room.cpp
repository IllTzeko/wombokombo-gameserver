#include "game/room.h"
#include "utils/logger.h"

namespace game {

Room::Room(std::string id, int max_players)
    : id_(std::move(id)), max_players_(max_players) {}

bool Room::add_player(const Player& player) {
    if (is_full()) return false;
    if (has_player(player.id)) return false;
    if (state_ != RoomState::WAITING) return false;

    players_.emplace(player.id, player);
    logger::info("player " + player.id + " (" + player.name + ") joined room " + id_);
    return true;
}

void Room::remove_player(const std::string& player_id) {
    auto it = players_.find(player_id);
    if (it == players_.end()) return;

    logger::info("player " + player_id + " left room " + id_);
    players_.erase(it);

    if (players_.empty()) {
        state_ = RoomState::FINISHED;
        logger::info("room " + id_ + " is now empty, marked finished");
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

void Room::set_player_ready(const std::string& player_id, bool ready) {
    auto it = players_.find(player_id);
    if (it == players_.end()) return;

    it->second.ready = ready;

    // Broadcast ready state to all
    broadcast({
        {"type", "player_ready_state"},
        {"player_id", player_id},
        {"ready", ready}
    });

    logger::debug("player " + player_id + " ready=" + (ready ? "true" : "false")
                  + " in room " + id_);
}

bool Room::all_ready() const {
    if (players_.empty()) return false;
    // Need at least 2 players to start
    if (players_.size() < 2) return false;

    for (const auto& [_, p] : players_) {
        if (!p.ready) return false;
    }
    return true;
}

void Room::handle_chat(const std::string& sender_id, const std::string& message) {
    auto player = get_player(sender_id);
    if (!player) return;

    // Broadcast chat to all room members
    broadcast({
        {"type", "chat_message"},
        {"player_id", sender_id},
        {"player_name", player->name},
        {"message", message}
    });
}

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

} // namespace game
