FROM ubuntu:24.04 AS builder

ARG WITH_QUANTLIB=OFF
ARG WITH_SQLITE=OFF
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ cmake ninja-build git ca-certificates \
    && if [ "$WITH_QUANTLIB" = "ON" ]; then apt-get install -y --no-install-recommends libquantlib0-dev; fi \
    && if [ "$WITH_SQLITE" = "ON" ]; then apt-get install -y --no-install-recommends libsqlite3-dev; fi \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . /src

ENV CMAKE_BUILD_PARALLEL_LEVEL=1
RUN cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DSCHD_WITH_QUANTLIB=${WITH_QUANTLIB} \
    -DSCHD_WITH_SQLITE=${WITH_SQLITE} \
    && cmake --build build --target schd --parallel 1

FROM ubuntu:24.04

ARG WITH_QUANTLIB=OFF
ARG WITH_SQLITE=OFF
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && if [ "$WITH_QUANTLIB" = "ON" ]; then apt-get install -y --no-install-recommends libquantlib0v5; fi \
    && if [ "$WITH_SQLITE" = "ON" ]; then apt-get install -y --no-install-recommends libsqlite3-0; fi \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --create-home --uid 10001 schd

COPY --from=builder /src/build/schd /usr/local/bin/schd

USER schd
WORKDIR /home/schd

ENTRYPOINT ["schd"]
