#!/bin/bash
# The game's own tests on a Linux install made by tools/updtest/linux.sh (~/updtest/A0):
# RetroAchievements' disc hashes, save states and rewind, the start screen, the features run.
set -u
cd ~/updtest/A0/RondoRecomp || exit 1
mkdir -p ~/updtest/feat
for t in --test-achievements --autotest --autotest-start --autotest-features; do
    args=("$t")
    [ "$t" = --autotest-features ] && args+=("$HOME/updtest/feat")
    rm -f rondo.log
    timeout 900 ./rondo "${args[@]}" > /dev/null 2>&1
    echo "== $t: exit $?"
    grep -E "achievements:|autotest|features: start|features: menu|features: Stage|features: Retro|features: controls:|^build|native" rondo.log | cut -c1-200
done
