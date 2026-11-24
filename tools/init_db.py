#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Creates a picsel.db in DIR without the setup dialog (for tests).
usage: init_db.py DIR [TARGET] [--selected] [--sort=name|date]"""
import os, sqlite3, sys
# Mirrors src/core/SelectionDb.h (kDbFileName, kAppId, kSchemaVersion, cfg::*)
# and sortModeToString(); tst_selectiondb runs this script and checks it.
args = [a for a in sys.argv[1:] if not a.startswith("--")]
opts = dict(a[2:].partition("=")[::2] for a in sys.argv[1:] if a.startswith("--"))
d = os.path.abspath(args[0])
target = os.path.abspath(args[1]) if len(args) > 1 else os.path.dirname(d)
config = [("app_id", "picsel"), ("schema_version", "1"), ("source_dir", d), ("target_dir", target),
          ("default_selected", "1" if "selected" in opts else "0")]
if "sort" in opts:
    if opts["sort"] not in ("name", "date"):
        sys.exit("init_db.py: --sort must be name or date")
    config.append(("sort_mode", opts["sort"]))
c = sqlite3.connect(os.path.join(d, "picsel.db"))
c.execute("CREATE TABLE IF NOT EXISTS config (key TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL)")
for k, v in config:
    c.execute("INSERT OR REPLACE INTO config VALUES(?, ?)", (k, v))
c.commit()
