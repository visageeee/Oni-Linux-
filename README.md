# Oni for Linux

A native Linux port of Bungie's 2001 game *Oni*, forked from [Oni-Linux-](https://github.com/eggplantredrage/Oni-Linux-). You need your own PC or Mac retail game data; the game assets are not included.

## Fork additions

- Mouse-look timing changes and a frame limiter for high-refresh displays.
- Optional OpenGL colour grading and distance fog.
- Experimental dynamic lighting across gameplay levels, including supported fixtures, door indicators, fire and vehicle lights. Police beacons cast rotating red and orange light.
- Experimental lights for muzzle flashes and supported weapon projectiles.

These graphics features are opt-in, require the Linux SDL/OpenGL renderer, and do not modify game assets.

From `build/bin`, enable scene lighting and weapon lights with:

```bash
ONI_RELIGHT=1 ONI_WORLD_LIGHT=1 ./Oni
```

Use either variable on its own to enable just that group. The world renderer supports up to 16 lights per surface on capable GPUs, with an eight-light fallback. Characters use approximate per-vertex lighting. Light sources and wall occlusion are approximations, not shadow maps; reflective and transparent surfaces retain their original lighting. The extra lighting adds rendering work and has not been performance tuned.

Enable optional distance fog with `ONI_DISTANCE_FOG=1`. `ONI_FOG_START` and `ONI_FOG_END` set distances in world units; `ONI_FOG_STRENGTH` ranges from 0 to 1. Fog includes the skybox and is applied before the HUD.

For optional full-screen colour grading, including the UI:

```bash
ONI_SHADER=warm ./Oni
```

`ONI_SHADER=neutral` selects a passthrough shader. If a shader cannot be initialized, the game falls back to the original rendering path. Check `startup.txt` for `[shader]`, `[world-light]` and `[scene-light]` diagnostics.

The frame limiter is enabled by default. Set `ONI_FPS_CAP=0` to disable it for comparison. See [HISTORY.md](HISTORY.md) for upstream changes.

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
