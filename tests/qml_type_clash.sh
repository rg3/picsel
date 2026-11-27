#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# QML type names are always capitalised, so ours share one namespace with
# the Qt modules our QML imports. When both define a name, Qt's type can
# win at run time without any error: QtQuick 6.11 added TextSelection and
# silently broke our singleton of that name. Fails if a type of the Picsel
# module is also exported by an imported Qt module (or one it imports).
#   qml_type_clash.sh PICSEL_MODULE_DIR QT_QML_DIR QML_SOURCE_DIR
# Skips (exit 77) when none of the imported modules are installed.
set -eu
ours_dir=$1
qt=$2
qml_src=$3
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Type names a module directory defines: QML files and singletons listed in
# its qmldir, and C++ types exported in its .qmltypes files.
types_of() {
    grep -E '^(singleton )?[A-Z][A-Za-z0-9_]* [0-9]' "$1/qmldir" \
        | sed -E 's/^singleton //; s/ .*//'
    for ti in $(sed -n 's/^typeinfo //p' "$1/qmldir"); do
        [ -f "$1/$ti" ] || continue
        grep -oE '"[A-Za-z0-9_.]+/[A-Z][A-Za-z0-9_]* [0-9]' "$1/$ti" | sed -E 's/^"[^/]*\///; s/ .*//'
    done
}

types_of "$ours_dir" | sort -u > "$tmp/ours"
if [ ! -s "$tmp/ours" ]; then
    echo "qml_type_clash: no types found in $ours_dir/qmldir; has the build changed shape?"
    exit 1
fi

# Imported Qt modules, then whatever their qmldir files import in turn
# ("depends" only orders plugin loading and makes no names visible).
grep -hE '^import [A-Za-z]' "$qml_src"/*.qml | awk '$2 != "Picsel" {print $2}' | sort -u > "$tmp/todo"
: > "$tmp/seen"
: > "$tmp/theirs"
found=0
while [ -s "$tmp/todo" ]; do
    module=$(head -n 1 "$tmp/todo")
    sed -i 1d "$tmp/todo"
    grep -qx "$module" "$tmp/seen" && continue
    echo "$module" >> "$tmp/seen"
    dir=$qt/$(echo "$module" | tr . /)
    if [ ! -f "$dir/qmldir" ]; then
        echo "qml_type_clash: $module is not installed, not checked"
        continue
    fi
    found=1
    types_of "$dir" | sed "s|\$| $module|" >> "$tmp/theirs"
    sed -nE 's/^(optional |default )?import ([A-Za-z0-9_.]+).*/\2/p' "$dir/qmldir" >> "$tmp/todo"
done
if [ "$found" = 0 ]; then
    echo "qml_type_clash: no imported Qt module found under $qt"
    exit 77
fi

status=0
while read -r name; do
    clash=$(awk -v n="$name" '$1 == n {print $2}' "$tmp/theirs" | sort -u | tr '\n' ' ')
    if [ -n "$clash" ]; then
        echo "qml_type_clash: $name is also a type in: $clash- rename ours"
        status=1
    fi
done < "$tmp/ours"
[ "$status" = 0 ] && echo "qml_type_clash: $(wc -l < "$tmp/ours") types checked against $(sort -u "$tmp/seen" | wc -l) modules"
exit $status
