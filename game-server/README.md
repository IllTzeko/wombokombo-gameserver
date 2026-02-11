# WomboCombo Game Server (C++)

Real-time WebSocket game server for WomboCombo, a 2D cooperative survival platformer.

## Phase 1 — Foundations

Current implementation:
- [x] uWebSockets server accepting WebSocket connections
- [x] Room system: create, join, leave
- [x] Broadcast messages to all room members
- [x] Chat messages in lobby
- [x] Ready state tracking
- [x] Health check endpoint (`/health`)
- [x] Server info endpoint (`/info`)
- [x] Dockerfile (multi-arch: amd64 + arm64)
- [x] HTML test client

## Quick Start

### Docker (recommended)

```bash
docker build -t wombocombo-gameserver .
docker run -p 9001:9001 wombocombo-gameserver
```

### Local Build

```bash
# Clone uWebSockets
mkdir -p third_party
git clone --depth 1 --recurse-submodules \
    https://github.com/uNetworking/uWebSockets.git third_party/uWebSockets

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)

# Run
PORT=9001 LOG_LEVEL=debug ./build/gameserver
```

Or use the dev script:

```bash
chmod +x scripts/run_dev.sh
./scripts/run_dev.sh          # normal
./scripts/run_dev.sh --asan   # with AddressSanitizer
./scripts/run_dev.sh --tsan   # with ThreadSanitizer
```

## Testing

Open `tests/test_client.html` in two browser tabs, connect both to the same room, and send chat messages.

## Environment Variables

| Variable | Default | Description |
|---|---|---|
| `PORT` | `9001` | WebSocket server port |
| `LOG_LEVEL` | `info` | `debug`, `info`, `warn`, `error` |
| `MAX_ROOMS` | `100` | Maximum concurrent rooms |
| `MAX_PLAYERS_PER_ROOM` | `4` | Max players per room |
| `REDIS_ADDR` | `localhost:6379` | Redis address (Phase 2+) |
| `REDIS_PASSWORD` | _(empty)_ | Redis password (Phase 2+) |

## Connection Flow

```
Client connects to:  ws://server:9001/ws?room=ROOM_ID&name=PLAYER_NAME

1. Server validates room availability
2. Generates a player ID (Phase 2: extracted from JWT)
3. Creates room if it doesn't exist
4. Sends "connected" to the new player
5. Broadcasts "player_joined" to others
6. Sends "lobby_state" to everyone
```

## Protocol (Phase 1)

See `DEV2_GAMESERVER.md` for the full protocol spec.
