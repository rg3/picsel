#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The README's dependency table and distro notes repeat, for packagers, the
# minimum Qt version and the Qt components CMakeLists.txt asks for. Checks
# that they still match.
#   readme_qt.sh SOURCE_DIR
set -eu
cmake=$1/CMakeLists.txt
readme=$1/README.md
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
status=0

min=$(sed -nE 's/^set\(PICSEL_QT_MIN ([0-9.]+)\)/\1/p' "$cmake")
components=$(sed -nE 's/^set\(picsel_qt_components (.*)\)/\1/p' "$cmake" | tr ' ' '\n' | sort)
if [ -z "$min" ] || [ -z "$components" ]; then
    echo "readme_qt: PICSEL_QT_MIN or picsel_qt_components not found in CMakeLists.txt"
    exit 1
fi

# Every Qt version the README names ("≥ 6.8", "Qt 6.8") is the minimum.
versions=$(grep -oE '(Qt|≥) ?6\.[0-9]+' "$readme" | grep -oE '6\.[0-9]+' | sort -u)
if [ -z "$versions" ]; then
    echo "readme_qt: the README names no Qt version"
    status=1
fi
for v in $versions; do
    if [ "$v" != "$min" ]; then
        echo "readme_qt: the README names Qt $v, but PICSEL_QT_MIN is $min"
        status=1
    fi
done

# The "| Qt | ≥ x | A, B, C; Test for the unit tests |" row, before the ";".
row=$(grep -E '^\| Qt \|' "$readme" | cut -d'|' -f4 | cut -d';' -f1 | tr ',' '\n' | tr -d ' ' | grep -v '^$' | sort)
if [ "$row" != "$components" ]; then
    echo "readme_qt: the README's Qt components differ from picsel_qt_components (- CMake, + README):"
    printf '%s\n' "$components" > "$tmp/cmake"
    printf '%s\n' "$row" > "$tmp/readme"
    diff -u "$tmp/cmake" "$tmp/readme" || true
    status=1
fi
exit $status
