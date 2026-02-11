#pragma once

#include <string>
#include <unordered_map>
#include <functional>
#include <optional>
#include <nlohmann/json.hpp>

#include "game/player.h"

namespace game {

enum class RoomState { WAITING, PLAYING, FINISHED };

inline std::string room_state_str(RoomState s) {
    switch (s) {
        case RoomState::WAITING:  return "waiting";
        case RoomState::PLAYING:  return "playing";
        case RoomState::FINISHED: return "finished";
    }
    return "unknown";
}

class Room {
public:
    using BroadcastFn = std::function<void(const std::string& player_id, const std::string& message)>;

    explicit Room(std::string id, int max_players = 4);

    // Player management
    bool add_player(const Player& player);
    void remove_player(const std::string& player_id);
    bool has_player(const std::string& player_id) const;
    std::optional<Player> get_player(const std::string& player_id) const;
    bool is_full() const;
    bool is_empty() const;
    int player_count() const;

    // Lobby
    void set_player_ready(const std::string& player_id, bool ready);
    bool all_ready() const;

    // Chat
    void handle_chat(const std::string& sender_id, const std::string& message);

    // Broadcasting — the Room doesn't own sockets; it calls this callback
    // to send a message to a specific player. The server sets this up.
    void set_broadcast_fn(BroadcastFn fn);
    void broadcast(const nlohmann::json& msg);
    void broadcast_except(const std::string& exclude_id, const nlohmann::json& msg);
    void send_to(const std::string& player_id, const nlohmann::json& msg);

    // Accessors
    const std::string& id() const { return id_; }
    RoomState state() const { return state_; }
    int max_players() const { return max_players_; }

    // Build lobby state message
    nlohmann::json lobby_state() const;

private:
    std::string id_;
    int max_players_;
    RoomState state_ = RoomState::WAITING;
    std::unordered_map<std::string, Player> players_;
    BroadcastFn broadcast_fn_;
};

} // namespace game
