# syntax=docker/dockerfile:1
#
# The Mob Survivor server image: the C++ server plus the static web client.
# Build:  docker build -t mob-survivor .
# Run:    docker run --rm -p 8080:8080 -v mob-survivor-data:/data mob-survivor
#
# Both stages use the same Debian base (pinned by digest), so the runtime's
# libstdc++ matches the compiler's.

FROM debian:trixie-slim@sha256:a99cfc517144bc59b1978475ec53b46ecabec7e43635402ee5b77cc54cd1b20a AS build
RUN apt-get update \
    && apt-get install -y --no-install-recommends g++ cmake make python3 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY include include
COPY src src
COPY tests tests
COPY web web
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --parallel \
    && ctest --test-dir build --output-on-failure

FROM debian:trixie-slim@sha256:a99cfc517144bc59b1978475ec53b46ecabec7e43635402ee5b77cc54cd1b20a
LABEL org.opencontainers.image.title="Mob Survivor" \
      org.opencontainers.image.description="Multiplayer mob-shooting web game: server and web client" \
      org.opencontainers.image.source="https://github.com/scshafe/mob-survivor-game"
COPY --from=build /src/build/mob-survivor-server /app/mob-survivor-server
COPY web /app/web
RUN mkdir /data && chown 65532:65532 /data
ENV MOB_SURVIVOR_BIND=0.0.0.0 \
    MOB_SURVIVOR_PORT=8080 \
    MOB_SURVIVOR_WEB=/app/web \
    MOB_SURVIVOR_DATA=/data
USER 65532:65532
EXPOSE 8080
VOLUME ["/data"]
ENTRYPOINT ["/app/mob-survivor-server"]
