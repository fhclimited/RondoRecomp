#!/bin/bash
# Builds the Linux release (x86-64; the Steam Deck and desktop Linux):
# dist/RondoRecomp-<version>-linux-x86_64.tar.gz. Run on Linux or in WSL, with podman: the
# build runs in an Ubuntu 22.04 container (tools/linux/Containerfile, built once and cached)
# so that the result runs on glibc 2.35 or newer. See tools/linux/build.sh and
# docs/distribution.md.
#
# usage: tools/package_release_linux.sh
set -euo pipefail
cd "$(dirname "$0")/.."
command -v podman >/dev/null || { echo "needs podman (sudo apt install podman)"; exit 1; }
IMG=localhost/rondorecomp-build:22.04
podman build -q -t "$IMG" -f tools/linux/Containerfile tools/linux >/dev/null
podman run --rm -v "$PWD:/src" -w /src "$IMG" tools/linux/build.sh
