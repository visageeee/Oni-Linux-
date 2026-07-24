# Porting OniARM64 to Linux x86_64

## TL;DR

This codebase already had a working Linux/SDL platform layer scaffolded in
(`Platform_SDL`, `find_package(SDL2/OpenGL/OpenAL)`, a Linux `BFW_FileManager`,
`CMAKE_SYSTEM_NAME STREQUAL "Linux"` branches, etc.) — it looks like it was
built with Linux in mind from early on, then development focus moved to
macOS/ARM64. It builds and links cleanly on Ubuntu 24.04 x86_64 with exactly
**one fix required**.

Verified in a clean container:
- `cmake ..` configures cleanly, finds OpenGL/OpenAL/SDL2/libavcodec/libavutil
  via pkg-config.
- `make -j` builds `Oni` and `onipack` end-to-end, zero errors.
- The resulting `Oni` binary is a proper x86-64 ELF, all shared libs resolve
  (`ldd` is clean).
- Ran it with `SDL_VIDEODRIVER=dummy` (no display in the sandbox) — it
  initializes OpenGL, searches for a game-data folder, fails gracefully with
  a clear log message (`game data folder not found`) instead of crashing.
  That's the expected/correct behavior with no `GameDataFolder` present.

## The one required fix

`BungieFrameWork/BFW_Source/BFW_LocalInput/BFW_LocalInput.c` uses `dladdr()` /
`Dl_info` to symbolicate a caller address for trace logging. On macOS's libc
these are exposed unconditionally. On glibc (Linux) they're gated behind
`_GNU_SOURCE`, which has to be defined **before any header is included in the
translation unit** — and because the project uses a precompiled header
(`BFW_MasterHeader.h`, force-included via `target_precompile_headers`), a
file-local `#define _GNU_SOURCE` at the top of the .c file is too late: the
PCH already froze glibc's feature-test macros before that file's own defines
run.

Fix: added `add_compile_definitions(_GNU_SOURCE)` in the Linux branch of
`OniProj/OniCMakeProjs/OniProj/CMakeLists.txt`, right next to where the
Linux-only `OS_SOURCE_FILES` are set. That's a build-system-level define, so
it's in effect before the PCH compiles.

Diff:

```diff
--- a/OniProj/OniCMakeProjs/OniProj/CMakeLists.txt
+++ b/OniProj/OniCMakeProjs/OniProj/CMakeLists.txt
     if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
             set(OS_SOURCE_FILES
                     ../../../BungieFrameWork/BFW_Source/BFW_DebuggerSymbols/BFW_DebuggerSymbols_Win32.c
                     ../../../BungieFrameWork/BFW_Source/BFW_FileManager/Platform_Linux/BFW_FileManager_Linux.c)
+            # dladdr()/Dl_info (used in BFW_LocalInput.c for symbolicated trace
+            # logging) are GNU extensions gated behind _GNU_SOURCE on glibc.
+            # Must be a compile definition (not a file-local #define) so it's
+            # in effect before the precompiled header fixes glibc's feature-
+            # test macros for the whole translation unit.
+            add_compile_definitions(_GNU_SOURCE)
```

## Build instructions (Ubuntu/Debian-based)

```bash
sudo apt-get install cmake build-essential libsdl2-dev libgl1-mesa-dev \
    libopenal-dev libavcodec-dev libavutil-dev pkg-config

mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
make -j$(nproc)
```

Binary lands at `build/bin/Oni`.

## What's NOT part of the Linux build (and doesn't need to be)

The macOS-only sources are correctly fenced behind `if(APPLE)` in both
CMakeLists, so none of this gets compiled or linked on Linux:
- Native Metal renderer (`metal_*.mm`) — OpenGL is the (only, and already
  functional) renderer on Linux.
- `Oni_Cinematic_macOS.mm` (AVFoundation intro/outro playback), 
  `Oni_DataSetup_macOS.mm` (Cocoa first-run data-folder picker),
  `Oni_RendererSelect_macOS.mm`, `Oni_UpdateCheck_macOS.mm`,
  `Oni_CrashReport_macOS.mm`.

None of these are things you need for a Linux build to work, but they are
UX niceties the macOS build has that Linux currently lacks:

1. **Game-data first-run picker** — on macOS this is a Cocoa file-picker
   dialog (`Oni_DataSetup_macOS.mm`). On Linux right now you just need to
   manually drop a `GameDataFolder` (from your PC or Mac retail disc) next
   to the `Oni` binary, or in the cwd — same search path logic
   (`ONi_BundlePath.c`), no code changes needed, just no friendly picker UI.
2. **Update checker** (`Oni_UpdateCheck_macOS.mm`) — not ported; harmless to
   skip, it's just a "new release available" nag.
3. **Crash reporter dialog** (`Oni_CrashReport_macOS.mm`) — not ported;
   crashes will just terminate normally (the Linux build still gets
   the debugger.txt / startup.txt log files, which is where the real
   diagnostic info lives anyway).
4. **Intro/outro cinematics** (`Oni_Cinematic_macOS.mm`, AVFoundation-based)
   — will currently no-op/skip on Linux since there's no Linux implementation
   registered. Gameplay is unaffected; you just won't get the bundled `.mov`
   intro/outro (which aren't included for Linux anyway — they're macOS
   bundle assets under `macos/assets/`).

None of these block gameplay. If you want them later, the pattern's already
there in `Platform_SDL/Oni_*_macOS.mm` to copy from — swap Cocoa/AVFoundation
for something like GTK/Qt + your existing ffmpeg-based playback, gated behind
`elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")` the same way `OS_SOURCE_FILES` is.

## Next steps to actually play it

1. Get your `GameDataFolder` (from a legit PC or Mac Oni install) onto the
   Linux box, next to the `Oni` binary (or per `ONi_BundlePath.c`'s search
   paths).
2. Run `./Oni` from a real X11/Wayland session (not headless) — SDL2 will
   pick the driver automatically.
3. Controller support: issue `#73` in this repo's own tracking (README)
   says it's "in progress on a branch" for the macOS build — worth checking
   if that same SDL-based controller code is shareable, since SDL2 game
   controller APIs are cross-platform.
