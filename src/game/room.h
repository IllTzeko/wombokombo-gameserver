#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <optional>
#include <chrono>
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
    using Clock = std::chrono::steady_clock;

    explicit Room(std::string id, int max_players = 4);

    // ── Player management ───────────────────────────
    bool add_player(const Player& player);
    void remove_player(const std::string& player_id);
    bool has_player(const std::string& player_id) const;
    std::optional<Player> get_player(const std::string& player_id) const;
    bool is_full() const;
    bool is_empty() const;
    int player_count() const;

    // ── Lobby ───────────────────────────────────────
    void set_player_ready(const std::string& player_id, bool ready);
    bool all_ready() const;

    // ── Chat ────────────────────────────────────────
    void handle_chat(const std::string& sender_id, const std::string& message);

    // ── Gameplay (Phase 2) ──────────────────────────
    void start_game();
    void update(float dt);
    void queue_input(const std::string& player_id,
                     int tick,
                     const std::vector<std::string>& actions);

    // ── Broadcasting ────────────────────────────────
    void set_broadcast_fn(BroadcastFn fn);
    void broadcast(const nlohmann::json& msg);
    void broadcast_except(const std::string& exclude_id, const nlohmann::json& msg);
    void send_to(const std::string& player_id, const nlohmann::json& msg);

    // ── Accessors ───────────────────────────────────
    const std::string& id() const { return id_; }
    RoomState state() const { return state_; }
    int max_players() const { return max_players_; }
    int current_tick() const { return tick_; }

    // ── Grace period for reconnection ───────────────
    bool should_cleanup() const;

    // ── State snapshots ─────────────────────────────
    nlohmann::json lobby_state() const;
    nlohmann::json game_state() const;

private:
    std::string id_;
    int max_players_;
    RoomState state_ = RoomState::WAITING;
    int tick_ = 0;

    std::unordered_map<std::string, Player> players_;
    BroadcastFn broadcast_fn_;

    // Track disconnected players for reconnection during PLAYING
    std::unordered_map<std::string, Player> disconnected_players_;

    // Grace period: keep room alive for 30s after last player leaves
    static constexpr int GRACE_SECONDS = 30;
    std::optional<Clock::time_point> empty_since_;

    // Spawn positions for up to 4 players
    static constexpr float spawn_positions_[][2] = {
        {200.0f, physics::GROUND_Y},
        {400.0f, physics::GROUND_Y},
        {600.0f, physics::GROUND_Y},
        {800.0f, physics::GROUND_Y}
    };
    int next_spawn_ = 0;
};

} // namespace game
