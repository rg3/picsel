#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Session::info() and Session::prepareMove() return maps that QML reads by
# key; a misspelt key is just undefined in QML. Checks that every key QML
# reads from them (info.<key>, plan.<key>) is one Session.cpp sets.
#   qml_keys.sh SOURCE_DIR
set -eu
src=$1/src/app
status=0

# Keys set inside one Session.cpp function, given the start of its signature.
keys_of() {
    sed -n "/^QVariantMap Session::$1(/,/^}/p" "$src/Session.cpp" \
        | grep -oE 'QStringLiteral\("[A-Za-z]+"\)' | sed -E 's/.*\("([A-Za-z]+)"\)/\1/' | sort -u
}

# Keys QML reads as <var>.<key>.
used_as() {
    cat "$src"/qml/*.qml | grep -oE "\\b$1\\.[A-Za-z]+" | sed "s/^$1\\.//" | sort -u
}

check() { # function, QML variable
    set_keys=$(keys_of "$1")
    if [ -z "$set_keys" ]; then
        echo "qml_keys: found no keys in Session::$1(); has the code changed shape?"
        status=1
        return
    fi
    for key in $(used_as "$2"); do
        if ! printf '%s\n' "$set_keys" | grep -qx "$key"; then
            echo "qml_keys: QML reads $2.$key, but Session::$1() never sets \"$key\""
            status=1
        fi
    done
}

check info info
check prepareMove plan
exit $status
