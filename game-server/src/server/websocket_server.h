#pragma once

#include <string>
#include <unordered_map>
#include <memory>

#include "utils/config.h"
#include "game/room.h"

// Forward declare uWS types to avoid header pollution
namespace uWS {
    template<bool SSL, bool isServer, typename USERDATA> struct WebSocket;
    struct Loop;
}

namespace server {

// Per-socket data attached to each WebSocket connection
struct PerSocketData {
    std::string player_id;
    std::string player_name;
    std::string room_id;
};

class WebSocketServer {
public:
    explicit WebSocketServer(const config::ServerConfig& cfg);

    // Start listening — blocks the calling thread
    void run();

private:
    // Room management
    game::Room* get_or_create_room(const std::string& room_id);
    game::Room* get_room(const std::string& room_id);
    void cleanup_empty_rooms();

    // Parse query string params from URL
    static std::unordered_map<std::string, std::string> parse_query(std::string_view url);

    config::ServerConfig cfg_;
    std::unordered_map<std::string, std::unique_ptr<game::Room>> rooms_;

    // Map player_id → their raw WebSocket pointer (void* to avoid template in header).
    // Only valid while the socket is open.
    std::unordered_map<std::string, void*> player_sockets_;
};

} // namespace server
