# picsel architecture

How picsel is put together, why, and the traps found while building it. Read
this before changing anything non-trivial; the README covers usage and build.

## Layers

```
src/core    no UI; unit-tested; links Qt Core/Gui only
src/image   decoding and caches (picsel_core) + video thumbnails (picsel_media, QtMultimedia)
src/app     Application, Session, FileListModel, QML (qml/), automation hook
```

- **Application** (`src/app/Application.*`): one per process. It owns the
  QML engine, the single-instance socket, and one window plus `Session` per
  open folder. It also shows the setup, picker and error windows.
- **Session** (`src/app/Session.*`): everything about one folder: its
  `SelectionDb`, `FileListModel`, two `ImageCache`s (thumbnails and full
  view), `VideoThumbnailer`, `DirWatcher`, the metadata thread pool and the
  `MoveJob`. QML talks only to `Session` and the model.
- **FileListModel**: the files in display order. An `Item` has a stable
  `uid` used in image-provider URLs, and a `thumbVersion` that QML watches
  to reload a tile.

## Data and persistence

- **`picsel.db`** in the source folder holds two tables:
  - `config(key, value)`: app_id, schema_version, source and target folders,
    default selection, sort mode.
  - `files(name, selected, size, mtime_ns, inode, exif_time, camera, dhash)`.
  - The `exif_time` and `camera` columns are a cache, valid only while size
    and mtime match. A NULL `exif_time` means "not read yet"
    (`FileMeta::scanned`). `dhash` is unused so far.
- **Durability:** WAL mode, `synchronous=FULL`, one transaction per selection
  change, so a `kill -9` loses nothing (tested). Bulk writes (metadata,
  removals after a move) are batched with timers into one transaction, so
  1000 fsyncs never block the UI.
- **Transactions** nest via SAVEPOINT (`SelectionDb::Transaction`).
- **Reconcile on open** (`DirScanner::reconcile`): DB rows and disk files are
  matched by name. A vanished row whose inode and size match a new name is a
  rename, and keeps its selection. `files()` reports read errors, and
  loading refuses to reconcile against a partial list, which would
  otherwise reset choices.
- **Live changes:** `DirWatcher` uses raw inotify (rename cookies, "complete
  on CLOSE_WRITE or 1 s settle"), and `Session::onFile*` applies them.
  Moved-away files are removed idempotently, whether the watcher or
  `MoveJob` reports them first.
- **Global settings** (columns, camera clock offsets, similarity thresholds,
  show-details) are stored in `~/.config/picsel/picsel.ini` via
  `core/Settings`. Camera offsets are cached per Session; a change is pushed
  to every open window by `Application`.

## Images and threads

- **`decodeImage`** reads the whole file into memory, then libjpeg-turbo
  decodes it:
  - DCT scaling picks the largest 1/2, 1/4 or 1/8 that still covers the
    target box, the main speed win.
  - BGRX output maps straight onto `QImage::Format_RGB32`.
  - The EXIF orientation is parsed from APP1 directly (no exiv2 on the hot
    path).
  - CMYK and other formats fall back to `QImageReader`.
  - Measured on 1000 × 24 MP (~7.7 MB) JPEGs over 16 threads: first screen in
    under 100 ms, all done in about 4 s. Entropy decoding is the floor. A
    further step would be the embedded previews (MPF) many cameras write.
- **`ImageCache`** is one generic class used twice:
  - Thumbnails: all threads, Fast quality, 4 GB budget, and the dHash
    computed on the worker.
  - Full view: 4 threads, High quality, ±3 around the current picture,
    paused while closed.
  - Scheduling: the job picked next is always the undecoded entry nearest
    the visible range. The budget evicts the farthest entries, never ones
    nearer than the candidate, and never injected video frames.
  - Per-entry generations drop stale in-flight results after `remove()` or
    `clear()` (a file replaced under the same name).
- **QML access:** QML gets images through `CacheImageProvider`
  (`image://thumb|full/<session>/<uid>/<version>`). Providers run on QML
  loader threads and only touch the thread-safe cache plus a mutex-guarded
  uid→name map. File names never appear in URLs.
- **Metadata:** exiv2 for photos and a hand-written ISO-BMFF `mvhd`/`tkhd`
  parser for MP4/MOV (`core/Mp4Info`) run on a 4-thread pool after a folder
  opens. Videos get a pseudo-camera such as "Videos (MP4)", so their clock
  can be corrected like a camera's.
- **Video:** `VideoThumbnailer` (QMediaPlayer + QVideoSink, 2 at a time, GUI
  thread). On Qt 6.11, `QVideoFrame::toImage()` already applies the
  rotation of portrait phone clips. The full view plays only the current
  page's video (`VideoPlayer.qml`), paused and muted at first.
- **Similar pictures:** a 64-bit dHash of each thumbnail. Neighbours in
  display order are linked when their Hamming distance is at most N bits
  and, when both times are known, they are at most M seconds apart. Runs of
  2+ become groups (`core/Similarity`).

## Moving files (`core/Mover`)

- **Preflight:** every selected name is checked before anything moves. A
  non-regular file at the target (folder, symlink, fifo…) refuses the whole
  move.
- **Same filesystem:** `renameat2(RENAME_NOREPLACE)`, with a linkat+unlink
  fallback where NOREPLACE isn't supported. Overwrite is a plain atomic
  `renameat` after re-checking that the target is a regular file.
- **EXDEV (other filesystem):**
  1. Copy (`copy_file_range`, falling back to read/write) into a
     `.picsel-tmp-*` file in the target folder.
  2. Copy the mode and timestamps, then `fsync`.
  3. Rename the temp file into place.
  4. `fsync` the target folder.
  5. Only then unlink the source.
- Runs on its own thread. A conflict blocks the worker until the GUI answers
  through `resolveConflict()`.

## UI notes

- **Style:** Qt Quick Controls Fusion, with an explicit dark `Theme.palette`
  set on every top-level window. Basic style, or only
  `QGuiApplication::setPalette`, let KDE's light per-control palettes leak
  in.
- **Selection keys:** S, D, T and Ctrl+Z are `Shortcut`s on the window, so
  they work in both the mosaic and the full view. They are disabled while a
  dialog is open. Arrow and other navigation keys are handled per view.
- **Single instance:** QLocalServer `picsel-$UID`. A second launch sends
  `{path, XDG_ACTIVATION_TOKEN}` and waits up to 10 s for "ok". `listen()`
  only removes a socket nobody answers on.

## Gotchas found the hard way

- **KDE taskbar icon:** Plasma ignores icon folders created after it started
  (the first install may create `~/.local/share/icons/hicolor`), so with
  `Icon=io.github.rg3.picsel` the icon can be missing until the next
  login. The install runs no cache updates (`update-desktop-database`,
  `kbuildsycoca6`): that is left to the user or to packaging scripts. The
  SVG is also embedded and set as the window icon.
- **App id:** `PICSEL_APP_ID` (`io.github.rg3.picsel`, top-level
  `CMakeLists.txt`) names the desktop file, icon, AppStream metainfo and the
  Wayland app_id (`setDesktopFileName`). Changing it breaks users' taskbar
  pins and file associations. The application *name* stays `picsel`, which
  keeps the settings in `~/.config/picsel/`.
- **Install RPATH:** `qt_standard_project_setup` appends `$ORIGIN` entries
  to `CMAKE_INSTALL_RPATH`; they are filtered out again because picsel
  installs no libraries and distro checks reject them.
- **Hidden run-time dependencies:** QML modules, the multimedia backend and
  codecs, and the SVG plugin don't show up as link dependencies.
  `picsel --check` (`src/app/SystemCheck.*`) reports them; keep its module
  list in sync with the QML imports and the README.
- **Folder dialogs:** picsel is a QGuiApplication, so KDE offers no native
  dialog and Qt Quick draws its own. That one needs a parent window: a
  parentless `FolderDialog` is silently never shown, which is why the start
  picker (`FolderPicker.qml`, used when there is no path, e.g. from the app
  menu) is a window hosting the dialog. Qt >= 6.10 also shows such dialogs
  as separate popup windows; the picker sets `popupType` to `Popup.Item`
  when the property exists, so the dialog fills its own window.
- **Natural sort:** `QCollator` ignores numeric mode in the C locale, so
  `compareNames` is a hand-written, locale-independent natural sort.
- **No mmap for decoding:** a file truncated by another program
  mid-decode would SIGBUS the whole app.
- **ffmpeg noise:** the backend is chatty. `qt.multimedia.ffmpeg*.info` is
  silenced in `main.cpp`. libav itself prints hardware-decode fallback lines
  ("No support for codec h264 profile 100", "Failed setup for format vaapi"),
  some at error level, straight to stderr. `VideoThumbnailer` drops libav to
  fatal-only once its first player has loaded the plugin, looking up
  `av_log_set_level` with `dlsym` (no libav headers needed).
  `QT_FFMPEG_DEBUG=1` keeps the full log.
- **Qt logs go to journald** when stderr is not a terminal. Set
  `QT_FORCE_STDERR_LOGGING=1` to capture QML warnings from scripted runs.
- **GridView origin:** after rows are removed above the view (e.g. a move),
  the GridView's `originY` is no longer 0. All scroll maths in `Mosaic.qml`
  must be relative to `originY`, or the few remaining pictures end up
  mid-screen or scrolled out of sight.
- **Aggregate init:** `FileMeta` is built positionally in tests
  (`{exifTime, camera, dhash}`), so add new fields at the end.

## Testing

- **Unit tests:** `ctest` in `build/`, 10 suites, about 27 s
  (`tst_watcher` ~16 s, `tst_videothumbnailer` ~9 s). The video tests
  generate fixtures with the ffmpeg CLI and skip without it. Two more tests
  validate the desktop file and metainfo when `desktop-file-validate` and
  `appstreamcli` are installed. `BUILD_TESTING=OFF` skips all of them (and
  the Qt Test dependency); `PICSEL_BUILD_TOOLS=ON` builds `bench_decode`.
- **UI checks without touching the desktop:**
  `PICSEL_AUTOMATION=script.txt picsel DIR` (`src/app/Automation.*`) injects
  key and mouse events into picsel's own window and saves screenshots.
  - Automation runs (and `PICSEL_STANDALONE=1`) skip the single-instance
    socket. Otherwise a test launch is handed to the user's running picsel and
    opens a window on their screen.
  - Click coordinates are *logical*. The dev machine runs at 2× scale, so
    halve screenshot pixels.
  - Keys and clicks are plain `QKeyEvent`s and `QMouseEvent`s sent to the
    window, so the app doesn't link Qt Test. Keys still trigger QML
    `Shortcut`s. (`QTest::mouseClick` on a QWindow did nothing on Wayland.)
  - `QT_QPA_PLATFORM=offscreen` runs scripts with no window on screen at
    all; `shot` still works.
  - Don't use `/dev/uinput` (ydotool and similar): keys would reach whatever
    window has focus.
- **Test data:**
  - `tools/make_testset.sh DIR [N] [VIDEOS]` builds synthetic pictures:
    pixels stored rotated, so they only appear upright if the EXIF
    orientation is honoured. With VIDEOS > 0 it also writes H.264 clips.
  - `tools/set_exif.py` writes a minimal EXIF block.
  - `tools/init_db.py DIR` skips the setup window.
  - `build/tools/bench_decode DIR` times thumbnail decoding.
