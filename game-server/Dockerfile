# ── Build stage ───────────────────────────────────
FROM alpine:3.20 AS build

RUN apk add --no-cache \
    build-base \
    cmake \
    git \
    zlib-dev \
    linux-headers

WORKDIR /src

# Fetch uWebSockets + uSockets
RUN mkdir -p third_party && \
    git clone --depth 1 --recurse-submodules https://github.com/uNetworking/uWebSockets.git third_party/uWebSockets

# Copy project files
COPY CMakeLists.txt vcpkg.json ./
COPY src/ src/

# Build
RUN cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-O2" && \
    cmake --build build --parallel $(nproc)

# ── Production stage ──────────────────────────────
FROM alpine:3.20

RUN apk add --no-cache \
    libstdc++ \
    zlib \
    curl

COPY --from=build /src/build/gameserver /usr/local/bin/gameserver

# Non-root user
RUN adduser -D -H gameserver
USER gameserver

ENV PORT=9001
EXPOSE 9001

HEALTHCHECK --interval=30s --timeout=3s --retries=3 \
    CMD curl -sf http://127.0.0.1:9001/health || exit 1

ENTRYPOINT ["gameserver"]
