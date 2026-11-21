# picsel

Pick which pictures (and videos) of a folder to keep, quickly, with the keyboard.

Typical use: dump an event's photos into `2026-11-21-Whatever/pending-selection/`,
right-click any picture in the file manager → **Open with picsel**, mark the
keepers (or the rejects), then press **Move** to move the selected ones into the
gallery folder (by default the parent). Delete `pending-selection/` yourself
when you are done.

## Using it

- Opening a picture opens its whole folder, with that picture highlighted.
  Running `picsel` with no arguments asks for a folder; `picsel DIR` works too.
- The first time, picsel asks for the destination folder and whether pictures
  start as selected or not selected. It then keeps `picsel.db` in the folder.
  Every change is saved immediately, so you can close picsel (or it can crash)
  and continue later.
- Files added, deleted or renamed in the folder while picsel runs are picked up
  automatically. New files get the folder's default state.

| Key | Action |
|---|---|
| `S` / `D` | select / deselect, then go to the next picture |
| `T` | toggle the selection without moving |
| Arrows | move (left/right wrap across rows) |
| Home / End, PgUp / PgDn | first / last, page up / down |
| Enter or double-click | full view (Esc or Enter closes) |
| Space / `M` | in the full view, play/pause a video / toggle its sound |
| `+` / `-` | bigger / smaller thumbnails (2–6 columns) |
| Ctrl+Z | undo selection changes |

The **Move** button checks every selected file first. If a folder, link or other
non-regular file has the same name in the destination, nothing is moved. For
existing pictures with the same name you choose overwrite, keep both (adds
`-1`, `-2`… to the name) or skip. Moves within one filesystem are instant
renames; across filesystems files are copied, synced to disk, and only then
removed from the source.

**Sort by date taken** uses EXIF dates (videos use their creation time). If a
camera's clock was off, fix it once under **Camera clocks…**; corrections are
global. **Similar pictures** marks bursts of look-alike shots taken a few
seconds apart with a coloured bar; the thresholds are adjustable.

Global settings live in `~/.config/picsel/picsel.ini`.

## Building

Fedora packages:

```
sudo dnf install qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtmultimedia-devel \
    qt6-qtshadertools-devel libjpeg-turbo-devel exiv2-devel sqlite-devel ninja-build
```

H.264 video playback needs the full ffmpeg (RPM Fusion).

```
cmake -S . -B build -G Ninja -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build build
(cd build && ctest)                 # unit tests
cmake --install build
update-desktop-database ~/.local/share/applications
```

## Development notes

- `src/core`: no UI — database (`SelectionDb`), folder scan and reconcile
  (`DirScanner`), inotify watcher (`DirWatcher`), mover (`Mover`), EXIF and MP4
  metadata, sorting, similarity.
- `src/image`: libjpeg-turbo decoding with DCT scaling (`JpegDecoder`), the
  prioritised background cache (`ImageCache`), video thumbnails.
- `src/app`: `Session` (one per open folder), the list model, QML in `qml/`.
- `tools/make_testset.sh DIR [N] [VIDEOS]` builds synthetic test folders,
  `tools/init_db.py DIR` skips the setup window, `build/tools/bench_decode DIR`
  times thumbnail decoding.
- `PICSEL_AUTOMATION=script.txt picsel DIR` plays a script of key presses,
  clicks (logical coordinates) and screenshots against picsel's own window; see
  `src/app/Automation.h`.
