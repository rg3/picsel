// SPDX-License-Identifier: GPL-3.0-or-later
// Global (not per-folder) settings, stored in ~/.config/picsel/picsel.ini.
#pragma once

#include "Sorting.h"

#include <QString>
#include <optional>

namespace picsel::settings {

// Range and default of an integer setting, declared once: reads and writes
// clamp to it, Session clamps to it, and QML gets the range from Session.
struct IntSetting {
    int min;
    int max;
    int fallback;
    constexpr int clamp(int v) const { return v < min ? min : v > max ? max : v; }
};
inline constexpr IntSetting kColumns{2, 6, 4};
inline constexpr IntSetting kSimilarSeconds{1, 60, 10};
inline constexpr IntSetting kSimilarBits{2, 24, 10};
static_assert(kColumns.clamp(kColumns.fallback) == kColumns.fallback);
static_assert(kSimilarSeconds.clamp(kSimilarSeconds.fallback) == kSimilarSeconds.fallback);
static_assert(kSimilarBits.clamp(kSimilarBits.fallback) == kSimilarBits.fallback);

int columns();
void setColumns(int n);

CameraOffsets cameraOffsets();
void setCameraOffset(const QString &camera, qint64 seconds);

int similarSeconds();       // max time gap inside a group of similar pictures
void setSimilarSeconds(int s);
bool showDetails();         // EXIF details in the side panel
void setShowDetails(bool on);
bool showSimilar();
void setShowSimilar(bool on);
int similarBits();          // max dHash Hamming distance inside a group
void setSimilarBits(int b);

// Whether a folder opens at the picture that was highlighted when its window
// was last closed: even when another picture is given (Always), only when a
// folder is given (Folder), or not at all (Never, picsel 0.1.0's behaviour).
enum class Resume { Always, Folder, Never };
// "always", "folder" or "never" (--resume, picsel.ini, QML); anything else gives nothing.
std::optional<Resume> parseResume(const QString &s);
QString resumeToString(Resume r);
Resume resume();
void setResume(Resume r);

// The row a window opens at. |given| is the row of the picture picsel was
// opened with, |remembered| the one restored from the last close (-1 for
// none). |exact| is false when the remembered picture is gone and
// |remembered| is the one now in its place: a given picture beats that guess.
int startRow(Resume mode, int given, int remembered, bool exact);

} // namespace picsel::settings
