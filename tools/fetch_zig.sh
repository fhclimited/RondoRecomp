#!/bin/bash
# Fetches the pinned Zig toolchain into build/zig/. Setup uses its C compiler (`zig cc`:
# Clang + LLD) to build rondo.exe on the player's machine. Same pin as LASRecomp.
set -euo pipefail
cd "$(dirname "$0")/.."
VER=0.17.0
TARGET=x86_64-windows
NAME=zig-$TARGET-$VER
# sha256 from ziglang.org/download/index.json
SHA=b5663f69581dcf391293fbf16c06cb80d81d806545ce618b4d0bab7f0eb8c428
DL=build/zig-dl
mkdir -p "$DL"
[ -f "$DL/$NAME.zip" ] || curl -fL --proto '=https' -o "$DL/$NAME.zip" "https://ziglang.org/download/$VER/$NAME.zip"
echo "$SHA  $DL/$NAME.zip" | sha256sum -c -
rm -rf build/zig "$DL/$NAME"
if command -v unzip >/dev/null; then unzip -q "$DL/$NAME.zip" -d "$DL"; else python -m zipfile -e "$DL/$NAME.zip" "$DL"; fi
mv "$DL/$NAME" build/zig
echo "zig $(build/zig/zig.exe version) in build/zig"
