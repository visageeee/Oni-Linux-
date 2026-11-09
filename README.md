

## Oni-Linux
## A Port Of The 2001 Bungie Game Oni TO Linux x86-84 
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

- ### Credits
- Original Source code used to port to linux
- https://github.com/andiyar/OniARM64
- 

### Experimental Linux shader

The existing renderer can optionally apply an OpenGL shader to the completed
frame (including UI). From `build/bin`, run `ONI_SHADER=warm ./Oni` for a subtle
warm colour grade, or `ONI_SHADER=neutral ./Oni` to check the passthrough path.
Run `./Oni` without the variable for the original renderer. Shader compilation
or resource failures fall back to the original frame; see `startup.txt` for
`[shader]` diagnostics. This is a fullscreen post-process, not per-material
lighting or a normal-map shader. It adds a full-resolution texture copy and draw.

### Experimental per-pixel weapon lights (Linux)

Run `ONI_WORLD_LIGHT=1 ./Oni` from `build/bin` to enable brief muzzle lights
on opaque world geometry. The normal launch leaves them off. This experiment
supports up to four recent shots at a time; transparent, environment-mapped,
character and UI geometry keep their original lighting. It does not cast
shadows, so light can pass through walls. It adds per-surface shader work and
has not been performance tuned. `[world-light]` messages in `startup.txt`
report shader activation or failure. It can be combined with `ONI_SHADER=warm`.

### Experimental Syndicate warehouse relighting (Linux)

From `build/bin`, run `ONI_WAREHOUSE_LIGHT=1 ./Oni` and load Syndicate
Warehouse. This blends down the baked vertex colours on opaque world surfaces
and adds up to eight nearby per-pixel fixture lights per surface. Characters use a separate per-vertex approximation of nearby lights. The level does not retain
fixture light records in the tested data, so the experiment instead places
proxy lights at `WH_LIGHT_FACE` surfaces and the warehouse’s `locklight` and `locklight2` indicator particles. The indicators follow their live particle colours; fixture strength and the white lights’ warm colour are approximations. There are no shadows or occlusion, and lights may pop or leak
through walls. Reflective and transparent world geometry retains its original lighting. The scan and per-surface shader work add overhead.

Set `ONI_WORLD_LIGHT=1` as well for weapon muzzle lights. Without the warehouse
variable the original level lighting is retained. See `[warehouse-light]`
in `startup.txt` for the number of detected proxies.
