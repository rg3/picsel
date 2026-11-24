#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Checks that the shared-mime-info file maps exactly "picsel.db" to picsel's
# type. update-mime-database exits 0 even on broken input, so look at what
# it generated instead.
#   mime_glob.sh UPDATE_MIME_DATABASE MIME_XML
set -eu
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
mkdir "$dir/packages"
cp "$2" "$dir/packages/picsel.xml"
"$1" "$dir" >/dev/null 2>&1
grep -qx '50:application/x-picsel-db:picsel.db:cs' "$dir/globs2"
