#!/bin/bash
# Assembles the release folder dist/RondoRecomp and dist/RondoRecomp-<version>-windows-x64.zip.
# The release contains no game material: players provide their own disc image to Setup.
#
# usage: tools/package_release.sh [version]
set -euo pipefail
cd "$(dirname "$0")/.."
VER=${1:-0.8.1}
B=build
LIC=/c/msys64/ucrt64/share/licenses

[ -x build/zig/zig.exe ] || tools/fetch_zig.sh
cmake --build $B

OUT=dist/RondoRecomp
rm -rf dist
mkdir -p "$OUT"
cp "$B/RondoRecomp Setup.exe" "$B/rondo_setup.exe" "$B/SDL3.dll" "$B/libiconv-2.dll" "$OUT/"
cp -r "$B/sdk" "$OUT/sdk"
cp -r build/zig "$OUT/zig"
cp docs/PLAYER_README.txt "$OUT/README.txt"

{
    echo "Third-party software included with RondoRecomp"
    echo "==============================================="
    echo
    echo "SDL 3 (SDL3.dll, sdk/include/SDL3, sdk/lib): zlib license"
    echo "    https://libsdl.org"
    echo "Zig $("$OUT/zig/zig.exe" version) (zig/): MIT; its LLVM, Clang and LLD: Apache 2.0 with LLVM exceptions"
    echo "    https://ziglang.org - see zig/LICENSE"
    echo "zlib (statically linked into the Setup programs): zlib license"
    echo "    https://zlib.net"
    echo "GNU libiconv (libiconv-2.dll, used by SDL3.dll): LGPL 2.1 or later"
    echo "    https://www.gnu.org/software/libiconv - source: https://packages.msys2.org"
    echo "Shinonome Gothic 12 and 16 dot bitmap fonts (sdk/src/runtime/pce/kanji_font.c, the"
    echo "Japanese text of the original disc): public domain, The Electronic Font Open Laboratory"
    echo "    http://openlab.jp/efont/shinonome/"
    echo
    echo "The license texts follow."
    for f in "$LIC/SDL3/LICENSE.txt" "$LIC/zlib/LICENSE" "$LIC/libiconv/COPYING.LIB"; do
        [ -f "$f" ] || continue
        echo
        echo "---------------------------------------------------------------------------"
        echo "$f" | sed 's|.*/licenses/||'
        echo "---------------------------------------------------------------------------"
        cat "$f"
    done
} > "$OUT/THIRD_PARTY.txt"

(cd dist && python -m zipfile -c "RondoRecomp-$VER-windows-x64.zip" RondoRecomp)
echo "release: $OUT ($(du -sh "$OUT" | cut -f1)), dist/RondoRecomp-$VER-windows-x64.zip ($(du -sh "dist/RondoRecomp-$VER-windows-x64.zip" | cut -f1))"
