# picsel

Qt 6 / QML / C++20 desktop app for choosing which pictures and videos of a folder to keep.
The original spec is in IDEA.md. Read docs/ARCHITECTURE.md before non-trivial changes: it
covers the design, the threading model and known traps.

- Build: `cmake -S . -B build -G Ninja -DCMAKE_INSTALL_PREFIX=$HOME/.local && cmake --build build`
- Tests: `(cd build && ctest)`. Install: `cmake --install build`.
- Only use packages from the Fedora 45 repos. If something must be installed, give the user
  the `sudo dnf install …` command to run; sudo needs their password.
- Check UI changes with `PICSEL_AUTOMATION` scripts (see `src/app/Automation.h`) and
  generated test folders (`tools/`), never by injecting input into the user's desktop.
- Code style: namespace `picsel`, 4-space indent, Qt types and naming, short comments that
  say why.
