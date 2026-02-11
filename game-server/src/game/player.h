#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace game {

struct Player {
    std::string id;
    std::string name;
    std::string display_name;
    bool ready = false;

    // Position (used in Phase 2+)
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    int health = 100;
    std::string state = "idle";
    std::string facing = "right";

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
            {"x", x},
            {"y", y},
            {"vx", vx},
            {"vy", vy},
            {"health", health},
            {"state", state},
            {"facing", facing}
        };
    }
};

} // namespace game
