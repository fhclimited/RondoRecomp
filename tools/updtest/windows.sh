#!/bin/bash
# Updater test on Windows (docs/distribution.md, Updates): a fresh install of dist/'s package
# (built once into work/updtest/A0), copied to work/updtest/A and updated by the game itself
# (rondo.exe --autotest-update) from a local server offering the same packages as a newer
# release. github: the real latest release, by rondo_setup --update, as if this were 0.9.
#   DISC=<folder with your disc's .cue> tools/updtest/windows.sh slim|full|badsum|broken|github
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd)
DISC=${DISC:-$REPO/work/release_test13/RondoRecomp/game/en}
VER=$(sed -n 's/^#define RONDO_VERSION "\(.*\)"/\1/p' "$REPO/tools/frontend/programs.h")
ZIGV=$(sed -n 's/^#define RONDO_ZIG_VERSION "\(.*\)"/\1/p' "$REPO/tools/frontend/programs.h")
mkdir -p "$REPO/work/updtest" && cd "$REPO/work/updtest"
MODE=$1
DIST=$REPO/dist
if [ ! -x A0/RondoRecomp/rondo.exe ]; then
    rm -rf A0 && mkdir A0 && python -m zipfile -e $DIST/RondoRecomp-$VER-windows-x64.zip A0
    (cd A0/RondoRecomp && ./rondo_setup.exe "$DISC" > setup.log 2>&1) || { echo "setup failed"; exit 1; }
fi
rm -rf A srv shots-$MODE && cp -r A0 A && mkdir -p srv shots-$MODE
R=A/RondoRecomp
if [ "$MODE" = github ]; then
    START=$(date +%s)
    RONDO_UPDATE_FROM=0.9 $R/rondo_setup.exe --update 2>&1 | tr '\r' '\n' | grep -v '^\s*$' | awk '!/%$/ || /100%$/' | tail -32
    echo "== exit ${PIPESTATUS[0]} after $(( $(date +%s) - START ))s"
    rm -f $R/rondo.log; timeout 300 $R/rondo.exe --autotest >/dev/null 2>&1
    grep -E "^build|native|autotest" $R/rondo.log
    exit 0
fi
python "$REPO/tools/updtest/make_server.py" srv 8765 $VER "$MODE" $DIST/RondoRecomp-$VER-windows-x64.zip \
    $DIST/RondoRecomp-$VER-windows-x64-update-zig$ZIGV.zip
python -m http.server 8765 --bind 127.0.0.1 --directory srv > server.log 2>&1 &
SERVER=$!
sleep 2
START=$(date +%s)
RONDO_UPDATE_URL=http://127.0.0.1:8765/latest.json RONDO_UPDATE_FROM=0.1 timeout 900 $R/rondo.exe \
    --autotest-update "$(cygpath -w "$PWD/shots-$MODE")"
echo "== $MODE: first game exit $? after $(( $(date +%s) - START ))s"
grep autotest-update $R/rondo_update.log
if grep -q "UPDATE DID NOT INSTALL\|NO UPDATE\|TIMED OUT" $R/rondo_update.log; then
    kill $SERVER
    echo "-- not installed; the installation as it is now:"
    sleep 2
    [ -f $R/update_marker.txt ] && echo "MARKER PRESENT (files were replaced)" || echo "marker absent: old files kept"
    [ -f $R/sdk/src/frontend/main.c ] && echo "sdk intact" || echo "SDK BROKEN"
    ls $R/update 2>/dev/null && echo "(update/ left)"
    rm -f $R/rondo.log
    timeout 300 $R/rondo.exe --autotest >/dev/null 2>&1
    grep -E "^build|native|autotest" $R/rondo.log
    exit 0
fi
# the game restarted as a new process: its --autotest writes rondo.log
for i in $(seq 1 150); do [ -f $R/rondo.log ] && grep -q "CPU cycles" $R/rondo.log && break; sleep 2; done
sleep 3
kill $SERVER
echo "-- the restarted game's --autotest:"
grep -E "^build|native|autotest" $R/rondo.log
[ -f $R/update_marker.txt ] && echo "marker: $(cat $R/update_marker.txt)" || echo "MARKER MISSING"
ls $R/update 2>/dev/null && echo "UPDATE FOLDER LEFT" || echo "update/ cleaned up"
