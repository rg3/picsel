#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The QML modules picsel imports are listed twice for packagers: in
# `picsel --check` (SystemCheck.cpp) and in the README's run-time table.
# Checks that both lists match the imports under src/app/qml.
#   qml_modules.sh SOURCE_DIR
set -eu
root=$1
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Modules imported by the QML files, minus picsel's own module.
cat "$root"/src/app/qml/*.qml | sed -nE 's/^import ([A-Za-z.]+).*/\1/p' \
    | grep -vx Picsel | sort -u > "$tmp/imports"

# The list SystemCheck.cpp loops over.
sed -n '/for (const char \*module : {/,/})/p' "$root/src/app/SystemCheck.cpp" \
    | grep -oE '"[A-Za-z.]+"' | tr -d '"' | sort -u > "$tmp/check"

# The README row "QML modules `A`, `B`… | starting at all".
grep -E '^\| QML modules ' "$root/README.md" | cut -d'|' -f2 \
    | grep -oE '`[A-Za-z.]+`' | tr -d '`' | sort -u > "$tmp/readme"

status=0
for list in check readme; do
    if ! diff -u "$tmp/imports" "$tmp/$list" > "$tmp/diff"; then
        echo "qml_modules: the $list list differs from the QML imports (- imports, + $list):"
        cat "$tmp/diff"
        status=1
    fi
done
[ -s "$tmp/imports" ] || { echo "qml_modules: found no imports"; status=1; }
exit $status
