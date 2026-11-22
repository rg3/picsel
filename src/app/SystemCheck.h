// SPDX-License-Identifier: GPL-3.0-or-later
// SystemCheck: `picsel --check`. Reports the run-time pieces that linking
// doesn't guarantee and distros package separately: QML modules, the Qt
// Multimedia backend and its codecs, and SVG icon support.
#pragma once

namespace picsel {

// Prints a report to stdout. Returns 0 when picsel can run, 1 when something
// it cannot start without is missing (codecs only degrade videos).
int runSystemCheck();

// True when folder dialogs will be the desktop's own, shown through the XDG
// desktop portal (Qt's portal theme, FileChooser portal >= 3 for folders).
bool portalFolderDialogs();

} // namespace picsel
