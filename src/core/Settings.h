// SPDX-License-Identifier: GPL-3.0-or-later
// Global (not per-folder) settings, stored in ~/.config/picsel/picsel.ini.
#pragma once

#include "Sorting.h"

#include <QString>

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

} // namespace picsel::settings
