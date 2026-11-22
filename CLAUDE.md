# picsel

Qt 6 / QML / C++20 desktop app for choosing which pictures and videos of a folder to keep.
The original spec is in IDEA.md. Read docs/ARCHITECTURE.md before non-trivial changes: it
covers the design, the threading model and known traps.

- Build: `cmake -S . -B build -G Ninja -DCMAKE_INSTALL_PREFIX=$HOME/.local -DPICSEL_BUILD_TOOLS=ON && cmake --build build`
- Tests: `(cd build && ctest)`. Install: `cmake --install build`.
- The project is meant to be packaged by any distro: keep the README's dependency tables,
  `picsel --check` (`src/app/SystemCheck.*`), the man page and the AppStream metainfo in sync
  with code changes (new QML imports, options, user-visible features).
- Check UI changes with `PICSEL_AUTOMATION` scripts (see `src/app/Automation.h`) and
  generated test folders (`tools/`), never by injecting input into the user's desktop.
- Code style: namespace `picsel`, 4-space indent, Qt types and naming, short comments that
  say why.
