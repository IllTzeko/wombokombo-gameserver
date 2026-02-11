#include "utils/config.h"
#include "utils/logger.h"
#include "server/websocket_server.h"

#include <csignal>

int main() {
    // Load configuration from environment
    auto cfg = config::ServerConfig::from_env();
    logger::set_level(cfg.log_level);

    logger::info("=== WomboCombo Game Server v0.1.0 ===");
    logger::info("port=" + std::to_string(cfg.port)
                 + " tick_rate=" + std::to_string(cfg.tick_rate)
                 + " log_level=" + cfg.log_level);

    // Create and run server (blocks)
    server::WebSocketServer ws_server(cfg);
    ws_server.run();

    // If run() returns, the server has stopped
    logger::info("server stopped");
    return 0;
}
