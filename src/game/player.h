#pragma once

#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace game {

// Simple 2D physics constants — must match the client's Phaser config
// Client ground: tiles at y=704 (center), top surface at y=688
// Player body: 32px tall, origin 0.5 → sprite.y when on ground = 688 - 16 = 672
namespace physics {
    constexpr float MOVE_SPEED    = 220.0f;   // px/s — matches client PLAYER_SPEED
    constexpr float JUMP_VELOCITY = -420.0f;  // px/s — matches client PLAYER_JUMP
    constexpr float GRAVITY       = 800.0f;   // px/s² — matches client physics gravity
    constexpr float GROUND_Y      = 672.0f;   // sprite center Y when standing on ground
    constexpr float MAP_WIDTH     = 1280.0f;
    constexpr float MAP_HEIGHT    = 720.0f;
}

struct Player {
    std::string id;
    std::string name;
    std::string display_name;
    bool ready = false;

    // Position & velocity
    float x = 100.0f;
    float y = physics::GROUND_Y;
    float vx = 0.0f;
    float vy = 0.0f;

    // Stats
    int health = 100;
    int max_health = 100;
    int gold = 0;

    // State
    std::string state = "idle";     // idle, running, jumping, falling, dead
    std::string facing = "right";   // left, right

    // Input queue — set each tick from the latest player_input message
    std::vector<std::string> pending_actions;
    int last_input_tick = 0;

    // ── Physics update ──────────────────────────────
    void process_input(float dt) {
        if (health <= 0) {
            state = "dead";
            vx = 0;
            return;
        }

        vx = 0;

        for (const auto& action : pending_actions) {
            if (action == "left") {
                vx = -physics::MOVE_SPEED;
                facing = "left";
            }
            if (action == "right") {
                vx = physics::MOVE_SPEED;
                facing = "right";
            }
            if (action == "jump" && on_ground()) {
                vy = physics::JUMP_VELOCITY;
            }
        }

        // Gravity
        vy += physics::GRAVITY * dt;

        // Integrate position
        x += vx * dt;
        y += vy * dt;

        // Ground collision
        if (y >= physics::GROUND_Y) {
            y = physics::GROUND_Y;
            vy = 0;
        }

        // Clamp to map bounds
        x = std::clamp(x, 0.0f, physics::MAP_WIDTH);

        // Update visual state
        if (!on_ground()) {
            state = vy < 0 ? "jumping" : "falling";
        } else if (std::abs(vx) > 0.1f) {
            state = "running";
        } else {
            state = "idle";
        }

        // Clear inputs after processing
        pending_actions.clear();
    }

    bool on_ground() const {
        return y >= physics::GROUND_Y - 0.1f;
    }

    // ── Spawn at a given position ───────────────────
    void spawn(float spawn_x, float spawn_y) {
        x = spawn_x;
        y = spawn_y;
        vx = 0;
        vy = 0;
        health = max_health;
        state = "idle";
    }

    // ── Serialization ───────────────────────────────
    nlohmann::json to_lobby_json() const {
        return {
            {"id", id},
            {"name", name},
            {"display_name", display_name},
            {"ready", ready}
        };
    }

    nlohmann::json to_game_json() const {
        return {
            {"id", id},
            {"x", std::round(x * 10.0f) / 10.0f},
            {"y", std::round(y * 10.0f) / 10.0f},
            {"vx", std::round(vx * 10.0f) / 10.0f},
            {"vy", std::round(vy * 10.0f) / 10.0f},
            {"health", health},
            {"state", state},
            {"facing", facing}
        };
    }
};

}