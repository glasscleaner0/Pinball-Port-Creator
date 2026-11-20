# SPDX-License-Identifier: GPL-3.0-only
# Linux tools only: Docker/Podman provide the same environment on supported hosts.
# Game source and user-supplied XP assets are mounted at run time, not baked in.
FROM debian:12-slim
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       build-essential bison flex texinfo libgmp-dev libmpfr-dev libmpc-dev \
       curl ca-certificates xz-utils patch python3 cmake ninja-build git file \
       zlib1g-dev p7zip-full \
    && rm -rf /var/lib/apt/lists/*
ENV PINBALL_SOURCE=/source \
    PINBALL_CACHE=/cache \
    PINBALL_OUTPUT=/output \
    PINBALL_JOBS=2
WORKDIR /source
ENTRYPOINT ["bash", "/source/tools/portable/bootstrap.sh"]
