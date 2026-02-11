#pragma once

#include <string>
#include <nlohmann/json.hpp>

#include "game/room.h"
#include "network/protocol.h"
#include "utils/logger.h"

namespace network {

// Handles a single parsed message from a player inside a room.
// Returns false if the message type is unrecognized (non-fatal).
inline bool handle_message(game::Room& room,
                           const std::string& player_id,
                           const nlohmann::json& msg) {
    std::string type = get_type(msg);
    if (type.empty()) {
        room.send_to(player_id, make_error(400, "Missing or invalid 'type' field"));
        return false;
    }

    // ── Heartbeat ───────────────────────────────
    if (type == "ping") {
        room.send_to(player_id, {{"type", "pong"}});
        return true;
    }

    // ── Lobby messages ────────────────────────────
    if (type == "player_ready") {
        bool ready = msg.value("ready", false);
        room.set_player_ready(player_id, ready);

        // Check if all players are ready → signal game start (Phase 2+)
        if (room.all_ready()) {
            logger::info("all players ready in room " + room.id() + " — game start pending (Phase 2)");
            room.broadcast({
                {"type", "all_ready"},
                {"message", "All players ready. Game will start soon."}
            });
        }
        return true;
    }

    if (type == "chat_message") {
        std::string message = msg.value("message", "");
        if (message.empty()) {
            room.send_to(player_id, make_error(400, "Empty chat message"));
            return false;
        }
        // Cap message length
        if (message.size() > 200) {
            message = message.substr(0, 200);
        }
        room.handle_chat(player_id, message);
        return true;
    }

    // ── Gameplay messages (Phase 2+) ──────────────
    if (type == "player_input") {
        // Will be implemented in Phase 2
        logger::debug("received player_input from " + player_id + " (ignored in Phase 1)");
        return true;
    }

    if (type == "player_action") {
        logger::debug("received player_action from " + player_id + " (ignored in Phase 1)");
        return true;
    }

    if (type == "buy_item") {
        logger::debug("received buy_item from " + player_id + " (ignored in Phase 1)");
        return true;
    }

    // Unknown message type
    logger::warn("unknown message type '" + type + "' from player " + player_id);
    room.send_to(player_id, make_error(400, "Unknown message type: " + type));
    return false;
}

} // namespace network
