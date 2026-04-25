# ── Stage 1: build ──────────────────────────────────────────────────────────
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        libsdl2-dev \
        libsdl2-mixer-dev \
        premake4 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cd platforms/premake && \
    config=release_x64 make client

# ── Stage 2: runtime ────────────────────────────────────────────────────────
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        libsdl2-2.0-0 \
        libsdl2-mixer-2.0-0 \
        libgl1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# client binary
COPY --from=builder /src/platforms/premake/build/client/parsec /app/parsec

# game data the client loads on boot
COPY parsec_root/ /app/parsec_root/
COPY openparsec-assets/ /app/openparsec-assets/

# entrypoint — writes bot.con from env vars, then launches headless client
COPY docker/entrypoint.sh /app/entrypoint.sh
RUN chmod +x /app/entrypoint.sh

# UDP ports used to communicate with the game server
EXPOSE 6581/udp 6582/udp

ENTRYPOINT ["/app/entrypoint.sh"]
