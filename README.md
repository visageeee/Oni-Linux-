# Oni for Linux

A native Linux port of Bungie's 2001 game *Oni*, forked from [Oni-Linux-](https://github.com/eggplantredrage/Oni-Linux-). You need your own PC or Mac retail game data; the game assets are not included.

## Fork additions

- Mouse-look timing changes and a frame limiter for high-refresh displays.
- Optional OpenGL colour grading.
- Experimental per-pixel muzzle-flash lighting.
- Experimental Syndicate Warehouse relighting, including fixture lights and coloured door-indicator coronas. Characters use approximate per-vertex lighting.

These lighting features are opt-in and require the Linux SDL/OpenGL renderer. They do not modify game assets.

From `build/bin`, enable muzzle lights with:

```bash
ONI_WORLD_LIGHT=1 ./Oni
```

To also try the warehouse relighting, load Syndicate Warehouse with:

```bash
ONI_WAREHOUSE_LIGHT=1 ONI_WORLD_LIGHT=1 ./Oni
```

The warehouse experiment uses approximate fixture positions and up to eight lights per surface. It has no shadow pass: light can pass through walls, while reflective and transparent world surfaces retain their original lighting. The lighting adds rendering work and has not been performance tuned.

For optional full-screen colour grading, including the UI:

```bash
ONI_SHADER=warm ./Oni
```

`ONI_SHADER=neutral` selects a passthrough shader. If a shader cannot be initialized, the game falls back to the original rendering path. Check `startup.txt` for `[shader]`, `[world-light]` and `[warehouse-light]` diagnostics.

The frame limiter is enabled by default. Set `ONI_FPS_CAP=0` to disable it for comparison. See [HISTORY.md](HISTORY.md) for upstream changes.

## Local work in progress

The following additions exist in the local working tree but **are not yet in the published source**:

- Relighting across gameplay levels, enabled with `ONI_RELIGHT=1`.
- Lights that follow supported weapon projectiles and active fires.
- Up to 16 per-pixel lights per surface on capable GPUs, with an eight-light fallback.
- Approximate wall occlusion using cached line-of-sight checks.
- Weapon-light response on characters and reduced door-indicator brightness.

## Build and run

Install the dependencies for your distribution:

**Ubuntu / Debian**

```bash
sudo apt-get install cmake build-essential libsdl2-dev libgl1-mesa-dev \
    libopenal-dev libavcodec-dev libavutil-dev pkg-config
```

**Fedora**

```bash
sudo dnf install cmake gcc gcc-c++ SDL2-devel mesa-libGL-devel openal-soft-devel pkgconf-pkg-config
# ffmpeg-devel requires RPM Fusion:
sudo dnf install https://download1.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm \
    https://download1.rpmfusion.org/nonfree/fedora/rpmfusion-nonfree-release-$(rpm -E %fedora).noarch.rpm
sudo dnf install ffmpeg-devel
```

**Arch Linux**

```bash
sudo pacman -S cmake base-devel sdl2 mesa openal ffmpeg pkgconf
```

Build with CMake:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
```

Place your retail `GameDataFolder` beside `build/bin/Oni`, then launch from an X11 or Wayland session:

```bash
cd build/bin
./Oni
```

The Linux port has been used on x86-64 Ubuntu 24.04 and Fedora 43. Save data and settings use `~/Library/Application Support/OniARM64/` unless existing state files are found beside the executable. There is currently no Linux game-data folder picker.

## Credits

Based on [eggplantredrage/Oni-Linux-](https://github.com/eggplantredrage/Oni-Linux-), which builds on [OniARM64](https://github.com/andiyar/OniARM64). See [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) for third-party notices.
