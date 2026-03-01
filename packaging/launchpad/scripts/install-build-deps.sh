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
    gnupg \
    llvm-20-dev \
    libzstd-dev \
    ninja-build \
    nlohmann-json3-dev \
    rsync \
    zlib1g-dev
