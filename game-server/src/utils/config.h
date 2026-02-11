#pragma once

#include <string>
#include <cstdlib>

namespace config {

struct ServerConfig {
    int port = 9001;
    int tick_rate = 20;
    int max_rooms = 100;
    int max_players_per_room = 4;
    std::string redis_addr = "localhost";
    int redis_port = 6379;
    std::string redis_password;
    std::string log_level = "info";

    static ServerConfig from_env() {
        ServerConfig cfg;

        if (auto* v = std::getenv("PORT"))
            cfg.port = std::stoi(v);
        if (auto* v = std::getenv("TICK_RATE"))
            cfg.tick_rate = std::stoi(v);
        if (auto* v = std::getenv("MAX_ROOMS"))
            cfg.max_rooms = std::stoi(v);
        if (auto* v = std::getenv("MAX_PLAYERS_PER_ROOM"))
            cfg.max_players_per_room = std::stoi(v);
        if (auto* v = std::getenv("REDIS_ADDR")) {
            std::string addr = v;
            // Parse host:port format
            auto colon = addr.find(':');
            if (colon != std::string::npos) {
                cfg.redis_addr = addr.substr(0, colon);
                cfg.redis_port = std::stoi(addr.substr(colon + 1));
            } else {
                cfg.redis_addr = addr;
            }
        }
        if (auto* v = std::getenv("REDIS_PASSWORD"))
            cfg.redis_password = v;
        if (auto* v = std::getenv("LOG_LEVEL"))
            cfg.log_level = v;

        return cfg;
    }
};

} // namespace config
