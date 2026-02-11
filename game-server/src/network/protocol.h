#pragma once

#include <string>
#include <optional>
#include <nlohmann/json.hpp>

namespace network {

// Parse an incoming JSON message. Returns nullopt if invalid JSON.
inline std::optional<nlohmann::json> parse_message(std::string_view raw) {
    try {
        return nlohmann::json::parse(raw);
    } catch (const nlohmann::json::parse_error&) {
        return std::nullopt;
    }
}

// Extract message type from a parsed message.
inline std::string get_type(const nlohmann::json& msg) {
    if (msg.contains("type") && msg["type"].is_string()) {
        return msg["type"].get<std::string>();
    }
    return "";
}

// Build standard error response
inline nlohmann::json make_error(int code, const std::string& message) {
    return {
        {"type", "error"},
        {"code", code},
        {"message", message}
    };
}

// Build connected response
inline nlohmann::json make_connected(const std::string& player_id, int server_tick) {
    return {
        {"type", "connected"},
        {"player_id", player_id},
        {"server_tick", server_tick}
    };
}

// Build player_joined event
inline nlohmann::json make_player_joined(const std::string& player_id, const std::string& player_name) {
    return {
        {"type", "player_joined"},
        {"player_id", player_id},
        {"player_name", player_name}
    };
}

// Build player_left event
inline nlohmann::json make_player_left(const std::string& player_id) {
    return {
        {"type", "player_left"},
        {"player_id", player_id}
    };
}

} // namespace network
