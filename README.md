# Oni-Linux
A native Linux x86_64 build is now working, built on top of the project's existing `Platform_SDL` layer. Chapter 1 verified fully playable end-to-end (level load, combat, save/load, mouse-look).

### Build

Dependencies (Ubuntu/Debian):

```bash
sudo apt-get install cmake build-essential libsdl2-dev libgl1-mesa-dev \
    libopenal-dev libavcodec-dev libavutil-dev pkg-config
```

Fedora:

```bash
sudo dnf install cmake gcc gcc-c++ SDL2-devel mesa-libGL-devel openal-soft-devel pkgconf-pkg-config
# ffmpeg-devel needs RPM Fusion:
sudo dnf install https://download1.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm \
                 https://download1.rpmfusion.org/nonfree/fedora/rpmfusion-nonfree-release-$(rpm -E %fedora).noarch.rpm
sudo dnf install ffmpeg-devel
```

Arch:

```bash
sudo pacman -S cmake base-devel sdl2 mesa openal ffmpeg pkgconf
```

Then:

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
make -j$(nproc)
```

Binary lands at `build/bin/Oni`. Drop your `GameDataFolder` (PC or Mac retail, auto-detected) next to it and run `./Oni` from a real X11/Wayland session.

### Known-good on

- Ubuntu 24.04, Fedora 43 (x86_64)

### Notes

- No in-game data-folder picker dialog on Linux yet (that's Cocoa-only on macOS) — place `GameDataFolder` next to the binary manually.
- Save/settings persist to `~/Library/Application Support/OniARM64/` (yes, `Library` — kept for path-resolution parity with the macOS build; the directory is created automatically now).
- If mouse-look feels off on a high-refresh-rate display, the built-in frame limiter should already handle it (see HISTORY.md); `ONI_FPS_CAP=0` disables it for comparison.
