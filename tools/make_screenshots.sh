#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Regenerates the AppStream screenshots (data/screenshots/*.png) from the
# synthetic demo folder of make_demo.py, with picsel running offscreen.
#   make_screenshots.sh [PICSEL] [OUTDIR]
# PICSEL defaults to build/src/app/picsel, OUTDIR to data/screenshots.
#
# picsel shows folder paths, so it runs in a private mount namespace where
# the demo lives at /home/demo instead of under a temporary folder.
set -euo pipefail

if [[ ${1:-} != --in-namespace ]]; then
    here=$(dirname "$(readlink -f "$0")")
    picsel=$(readlink -f "${1:-$here/../build/src/app/picsel}")
    out=$(readlink -f "${2:-$here/../data/screenshots}")
    work=$(mktemp -d)
    trap 'rm -rf "$work"' EXIT
    mkdir -p "$work/host-home" "$work/demo" "$work/shots"
    unshare --user --map-root-user --mount "$here/make_screenshots.sh" --in-namespace \
        "$here" "$picsel" "$work"
    mkdir -p "$out"
    cp "$work"/shots/*.png "$out/"
    ls -l "$out"/*.png
    exit
fi

# Inside the namespace. The tmpfs over /home hides the real one, so files
# from there (picsel, these tools) are reached through a bind mount of it.
here=$2 picsel=$3 work=$4
mount --rbind /home "$work/host-home"
mount -t tmpfs tmpfs /home
mkdir /home/demo
mount --bind "$work/demo" /home/demo
host() { if [[ $1 == /home/* ]]; then echo "$work/host-home/${1#/home/}"; else echo "$1"; fi; }
here=$(host "$here") picsel=$(host "$picsel")

dest=$("$here/make_demo.py" /home/demo | tail -1)
shots=$work/shots

# Sorted by date: 0412 | 0418 0419 0420 (burst) | 0425 | 0431 | 0447 0448
# (burst) | 0452 | 0460 | 0466 0467 0468 (burst) | 0473 | 0480 | 0485 (video)
# | 0491 | 0497. Mark a mix (S selects, D deselects, both move on), then go
# to the misty dawn burst for the mosaic, and to the sunset for the full view.
cat > "$work/script.txt" <<SCRIPT
resize 1600 1000
wait 1500
key Minus
key Home
key S
key S
key D
key D
key S
key D
key S
key D
key S
key S
key D
key S
key D
key S
key S
key S
key D
key S
wait 2500
key Home
key Right
key Right
key Right
key Down
key Down
key Right
wait 1500
shot $shots/mosaic.png
key Left
key Left
key Left
key Up
key Right
key Right
key Right
key Return
wait 2000
shot $shots/fullview.png
key Escape
wait 800
click 180 960
wait 1000
shot $shots/move.png
quit
SCRIPT

# Fixed time zone and fresh settings: the same result on any machine, and
# the user's own settings are neither used nor changed.
mkdir -m 700 "$work/run"
HOME=/home/demo TZ=UTC XDG_CONFIG_HOME="$work/config" XDG_CACHE_HOME="$work/cache" \
    XDG_RUNTIME_DIR="$work/run" QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1 \
    PICSEL_STANDALONE=1 PICSEL_AUTOMATION="$work/script.txt" "$picsel" "$dest" >/dev/null
