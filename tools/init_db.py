#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Creates a picsel.db in DIR without the setup dialog (for tests).
usage: init_db.py DIR [TARGET] [--selected]"""
import os, sqlite3, sys
args = [a for a in sys.argv[1:] if not a.startswith("--")]
d = os.path.abspath(args[0])
target = os.path.abspath(args[1]) if len(args) > 1 else os.path.dirname(d)
c = sqlite3.connect(os.path.join(d, "picsel.db"))
c.execute("CREATE TABLE IF NOT EXISTS config (key TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL)")
for k, v in [("app_id", "picsel"), ("schema_version", "1"), ("source_dir", d),
             ("target_dir", target), ("default_selected", "1" if "--selected" in sys.argv else "0")]:
    c.execute("INSERT OR REPLACE INTO config VALUES(?, ?)", (k, v))
c.commit()
