#!/usr/bin/env bash
# Configure and build the JOCKY compiler into ./build.
set -euo pipefail
cd "$(dirname "$0")"
cmake -S . -B build -G Ninja "$@"
cmake --build build
echo "built: build/jockyc  (runtime: build/libjocky_rt.a)"
