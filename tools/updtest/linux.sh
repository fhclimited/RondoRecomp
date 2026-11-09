#!/bin/bash
# Updater test on Linux (WSL too): like windows.sh, in ~/updtest (in WSL, the Linux file
# system); it starts its own server. github: the real latest release, as if this were 0.9.
#   DISC=<folder with your disc's .cue> tools/updtest/linux.sh slim|full|badsum|broken|github
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd)
DIST=$REPO/dist
DISC=${DISC:-$REPO/work/release_test13/RondoRecomp/game/en}
VER=$(sed -n 's/^#define RONDO_VERSION "\(.*\)"/\1/p' "$REPO/tools/frontend/programs.h")
ZIGV=$(sed -n 's/^#define RONDO_ZIG_VERSION "\(.*\)"/\1/p' "$REPO/tools/frontend/programs.h")
MODE=$1
mkdir -p ~/updtest && cd ~/updtest
if [ ! -x A0/RondoRecomp/rondo ]; then
    rm -rf A0 && mkdir A0 && tar -xzf $DIST/RondoRecomp-$VER-linux-x86_64.tar.gz -C A0
    (cd A0/RondoRecomp && ./rondo_setup "$DISC" > setup.log 2>&1) || { echo "setup failed"; tail A0/RondoRecomp/setup.log; exit 1; }
fi
rm -rf A srv shots-$MODE && cp -a A0 A && mkdir -p srv shots-$MODE
R=A/RondoRecomp
if [ "$MODE" = github ]; then   # the real release on GitHub, as if this were 0.9
    START=$(date +%s)
    RONDO_UPDATE_FROM=0.9 $R/rondo_setup --update 2>&1 | tr '\r' '\n' | grep -v '^\s*$' | awk '!/%$/ || /100%$/' | tail -32
    echo "== exit ${PIPESTATUS[0]} after $(( $(date +%s) - START ))s"
    rm -f $R/rondo.log; timeout 300 $R/rondo --autotest >/dev/null 2>&1
    grep -E "^build|native|autotest" $R/rondo.log
    ls -l $R/rondo $R/rondo_setup "$R/RondoRecomp Setup" $R/zig/zig | cut -c1-10,40-
    exit 0
fi
python3 "$REPO/tools/updtest/make_server.py" srv 8766 $VER "$MODE" $DIST/RondoRecomp-$VER-linux-x86_64.tar.gz \
    $DIST/RondoRecomp-$VER-linux-x86_64-update-zig$ZIGV.tar.gz
python3 -m http.server 8766 --bind 127.0.0.1 --directory srv > server.log 2>&1 &
SERVER=$!
sleep 1
START=$(date +%s)
# on Linux the game restarts in the same process (execv): this returns after the new game's --autotest
RONDO_UPDATE_URL=http://127.0.0.1:8766/latest.json RONDO_UPDATE_FROM=0.1 timeout 900 $R/rondo \
    --autotest-update "$PWD/shots-$MODE" > game.out 2>&1
echo "== $MODE: exit $? after $(( $(date +%s) - START ))s"
kill $SERVER
grep autotest-update $R/rondo_update.log
if grep -q "UPDATE DID NOT INSTALL\|NO UPDATE\|TIMED OUT" $R/rondo_update.log; then
    echo "-- not installed; the installation as it is now:"
    [ -f $R/update_marker.txt ] && echo "MARKER PRESENT (files were replaced)" || echo "marker absent: old files kept"
    [ -f $R/sdk/src/frontend/main.c ] && echo "sdk intact" || echo "SDK BROKEN"
    ls $R/update 2>/dev/null && echo "(update/ left)"
    rm -f $R/rondo.log
    timeout 300 $R/rondo --autotest >/dev/null 2>&1
fi
echo "-- --autotest:"
grep -E "^build|native|autotest" $R/rondo.log
[ -f $R/update_marker.txt ] && echo "marker: $(cat $R/update_marker.txt)" || echo "no marker"
ls $R/update 2>/dev/null && echo "UPDATE FOLDER LEFT" || echo "no update/ folder"
ls -l $R/rondo $R/rondo_setup "$R/RondoRecomp Setup" $R/libSDL3.so.0 | cut -c1-10,40-
mkdir -p $REPO/work/updtest/linux-shots-$MODE && cp shots-$MODE/*.bmp $REPO/work/updtest/linux-shots-$MODE/ 2>/dev/null
