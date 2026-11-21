// Global (not per-folder) settings, stored in ~/.config/picsel/picsel.conf.
#pragma once

#include "Sorting.h"

#include <QString>

namespace picsel::settings {

int columns();              // 2..6, default 4
void setColumns(int n);

CameraOffsets cameraOffsets();
void setCameraOffset(const QString &camera, qint64 seconds);

int similarSeconds();       // max time gap inside a group of similar pictures
void setSimilarSeconds(int s);
int similarBits();          // max dHash Hamming distance inside a group
void setSimilarBits(int b);

} // namespace picsel::settings
