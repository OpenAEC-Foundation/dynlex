#!/bin/bash
set -euo pipefail

sudo apt update
sudo apt install -y \
    build-essential \
    clang-20 \
    debhelper \
    devscripts \
    dput \
    cmake \
    git \
    gnupg \
    ninja-build \
    nlohmann-json3-dev \
    python3 \
    rsync \
    tar
