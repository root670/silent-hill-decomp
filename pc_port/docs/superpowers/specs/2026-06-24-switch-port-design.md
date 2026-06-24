# Silent Hill — Nintendo Switch Port Design

**Date:** 2026-06-24  
**Branch:** `switch-port` (off `mac-port-clean`)  
**Target:** Homebrew NRO via devkitpro aarch64 + libnx

---

## Overview

Port the Silent Hill PC port to Nintendo Switch as a homebrew NRO. The Switch port reuses the existing `pc_port/` CMake build system and PsyCross SDL2+OpenGL abstraction — devkitpro portlibs expose the same SDL2/OpenAL/OpenGL ES API surface. Only three layers need Switch-specific additions: PsyCross (GLES renderer selection + offscreen fix), the pc_port CMakeLists (toolchain + exclusions), and two new platform files in `pc_port/switch/`.

Reference implementations:
- `~/REDRIVER2-switch` — Switch port of Driver 2, same PsyCross base; primary reference for all Switch-specific patterns
- `~/switch-examples` — devkitpro homebrew examples
- `remotes/pcport/xbox-port` — Xbox port branch; reference for platform directory structure and static map overlay pattern

---

## Branch Strategy

Base: `mac-port-clean`. This branch already carries:
- `pc_crash.c` excluded on non-WIN32
- OpenAL include guards
- Apple/clang warning suppressions (`-Wno-implicit-function-declaration` etc.)
- LP64 `long` fixes and aarch64 `DECLARE_P_ADDR` fix in PsyCross

All of these transfer directly to aarch64 Switch.

---

## Build System

### Toolchain

devkitpro ships a first-class CMake toolchain at `/opt/devkitpro/cmake/Switch.cmake`. Invoking CMake with it sets:
- Compiler: `aarch64-none-elf-gcc` / `g++`
- Arch flags: `-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft`
- `NINTENDO_SWITCH=TRUE`, `__SWITCH__` defined
- Linker specs: `switch.specs` (libnx startup)
- `nx_create_nro()` CMake helper for NRO packaging

### Build Script

`pc_port/build_switch.sh`:
```bash
#!/usr/bin/env bash
set -euo pipefail
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
cmake -B build_switch \
  -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build_switch -j"$(nproc)"
```

Output: `build_switch/SilentHillPC.nro`

### Portlib Prerequisites (one-time)

```bash
sudo dkp-pacman -Sy switch-sdl2 switch-openal-soft switch-libjpeg-turbo
```

### NRO Packaging

In `pc_port/CMakeLists.txt`, under `if(NINTENDO_SWITCH)`:
```cmake
nx_create_nro(SilentHillPC ICON "${CMAKE_SOURCE_DIR}/switch/icon.jpg")
```
This runs `elf2nro` + `nacptool` as a post-build step, producing the final `.nro`.

---

## PsyCross Changes

Three targeted changes to `~/PsyCross` (root670 fork). All derived from the `REDRIVER2-switch` PsyCross as reference.

### 1. `src/platform.h` — SDL include branch

Add `__SWITCH__` alongside Linux/Android so it uses `<SDL.h>` (portlibs path, not `<SDL2/SDL.h>`):

```c
// before:
#elif defined(_WINDOWS) || defined(__MINGW32__) || defined(__linux__) || defined(__ANDROID__) || defined(__RPI__)
#   include <SDL.h>

// after:
#elif defined(_WINDOWS) || defined(__MINGW32__) || defined(__linux__) || defined(__ANDROID__) || defined(__RPI__) || defined(__SWITCH__)
#   include <SDL.h>
```

### 2. `include/PsyX/PsyX_render.h` — renderer selection

Add `__SWITCH__` block selecting OpenGL ES 3 (Switch supports GLES 3 via EGL; OGLES_VERSION=3 enables PBO support):

```c
#elif defined(__SWITCH__)
#   define RENDERER_OGLES
#   define OGLES_VERSION (3)
```

### 3. `src/render/PsyX_render.cpp` — GLES offscreen readback fix

In `GR_SetOffscreenState`, the current code calls `PBO_Download` (which uses `glGetTexImage`) in the offscreen-disable path. `glGetTexImage` does not exist in GLES. Apply the fix from REDRIVER2 commit `ec4dc2d`:

- Condition the `USE_OFFSCREEN_BLIT` blit block on `&& !defined(RENDERER_OGLES)`
- Add a `#if defined(RENDERER_OGLES)` branch that uses `glReadPixels` + `glTexSubImage2D` to read back the offscreen render target

This fix is required for shadow rendering and any scene using the offscreen RT.

### 4. `CMakeLists.txt` — Switch build config

Under `if(NINTENDO_SWITCH)`:
- Skip `find_package(SDL2)` and `find_package(OPENAL)` — resolved via portlibs pkg-config
- Exclude `src/render/glad.c` (EGL/GLES headers come from portlibs, no GLAD loader needed)

---

## pc_port CMakeLists Changes

All under `if(NINTENDO_SWITCH)` blocks.

### Source exclusions

| File | Reason |
|------|--------|
| `main_pc.c` | Replaced by `switch/main_switch.c` |
| `map_overlay_loader.c` | Replaced by `switch/map_switch.c` (static pointer) |
| `dll_loader.c` | No dynamic loading on Switch; map_switch.c bypasses it entirely |
| `pc_crash.c` | Already excluded for non-WIN32 in mac-port-clean |

`fmv/fmv_player.cpp` is **included** — only needs one include guard fix (see FMV section below).

### Linker additions

```cmake
if(NINTENDO_SWITCH)
    target_link_libraries(SilentHillPC PRIVATE EGL GLESv2 glapi drm_nouveau nx m)
endif()
```

SDL2 and OpenAL are linked via portlibs pkg-config (handled by the Switch.cmake toolchain).

### Skip ENABLE_EXPORTS

The `ENABLE_EXPORTS` / `--export-all-symbols` flags are for map DLL loading and must not be set on Switch (no map DLLs, and the flag has no meaning under libnx).

---

## New Files: `pc_port/switch/`

### `switch/main_switch.c`

Entry point for the Switch build. Mirrors `main_pc.c` and `xbox_port/src/main_xbox.c`.

Responsibilities (in order):
1. `socketInitializeDefault()` + `nxlinkStdio()` — redirect stdout/stderr to nxlink host over TCP
2. `PsyX_Log_SetStream(stdout)` — wire all `SH_DBG`/PsyCross log output through stdout so nxlink captures it
3. `EnsureSwitchPathExists("sdmc:/switch/SilentHill")` + `chdir("sdmc:/switch/SilentHill")` — anchor all relative file I/O to the SD card directory
4. `FsPC_Init("gamedata")` — same relative path as PC port, now resolved via chdir
5. Standard init sequence: `PsxMemory_Init`, anim info builders, `SpuInit`, `ResetGraph`, `PsyX_InitSystem`, `MainLoop`
6. On exit: `ShutdownNxlinkStdio()`, `socketExit()`

Also includes a `ShowSwitchErrorScreen(title, message)` helper using `consoleInit` for fatal errors before SDL is up (e.g. missing SD card, missing disc image).

### `switch/map_switch.c`

Static map overlay pointer — identical pattern to `xbox_port/src/map_xbox.c`:

```c
#include "common.h"
#include "game.h"
#include "bodyprog/map/map.h"

extern s_MapOverlayHdr g_MapOverlayHeader_map0_s00;
s_MapOverlayHdr* g_pMapOverlayHeader = &g_MapOverlayHeader_map0_s00;
```

This bypasses `map_overlay_loader.c` and `dll_loader.c` entirely. Only map0_s00 (the starting map) is available; all other maps require `SH_BUILD_MAP_DLLS` which is not supported on Switch.

### `switch/icon.jpg`

256×256 JPEG homebrew icon for the NRO, displayed in hbmenu. Can use REDRIVER2's icon as a placeholder until a proper Silent Hill icon is created.

---

## FMV

`fmv/fmv_player.cpp` compiles on Switch with one change:

**Guard the `glad.h` include** — on Switch, SDL2 portlibs already includes GLES headers; including `glad.h` after them triggers a `__gl2_h_` / `__gl3_h_` redefinition error:

```cpp
// before:
#include <PsyX/common/glad.h>

// after:
#if defined(RENDERER_OGL)
#include <PsyX/common/glad.h>
#endif
```

All other dependencies work unchanged:
- `libjpeg` — available via `switch-libjpeg-turbo` portlib
- `SDL_QueueAudio` — SDL2 portlib
- `mdec.c` / `str_demux.c` — pure C, no platform dependencies

**AVI files are recommended but not required.** Without them, `FMV_Play` falls back to `PlayFromBin` (software MDEC decode from the BIN). Video plays correctly; FMV audio sync from BIN is noted as incomplete in the source — AVIs provide properly synced audio.

---

## Filesystem & SD Card Layout

`main_switch.c` calls `chdir("sdmc:/switch/SilentHill")` at startup. All relative `fopen` calls (game assets via `FsPC_*`, FMV search paths, config, saves) resolve under this directory automatically.

```
sdmc:/switch/SilentHill/
  SilentHill.nro              ← homebrew binary
  config.cfg                  ← auto-created on first run
  gamedata/
    Silent Hill (USA).bin     ← required: disc image
    save/                     ← auto-created on first run
      0.MCD                   ← memory card slot 1
      1.MCD                   ← memory card slot 2
    fmv/                      ← recommended: pre-extracted AVI overrides
      C1_20670.avi
      C2_20670.avi
      M1_03500.avi
      ... (30 files total — see docs/fmv_files.md)
```

The disc image filename is auto-detected: any `.bin` in `gamedata/` is accepted, with `Silent Hill (USA).bin` / `Silent Hill (PAL).bin` tried first.

---

## Input

SDL2's `SDL_GameController` API works on Switch via portlibs with no changes to PsyCross. Joy-Con (held) and Pro Controller both enumerate as game controllers. The existing PSX pad mapping in PsyCross maps naturally to Switch face buttons.

---

## Stdio / Debug Output via nxlink

Development workflow for stdio redirection:

**Switch side** (`main_switch.c`):
```c
socketInitializeDefault();
nxlinkStdio();           // redirects stdout/stderr to nxlink host
PsyX_Log_SetStream(stdout);  // SH_DBG → stdout → nxlink
```

**Host side** (after Switch launches the NRO via hbmenu):
```bash
# Send NRO to Switch and open stdio relay in terminal
nxlink -a <switch_ip> build_switch/SilentHillPC.nro

# Or attach stdio relay to already-running app
nxlink -p <switch_ip>
```

hbmenu has nxlink support built-in — no separate stub needed on the Switch side.

---

## Known Limitations

- **Map overlays**: Only `map0_s00` (the starting map) is available. Dynamic map loading via `dll_loader` is not supported on Switch. Full map support requires static linking of all maps, which has 500+ symbol collision issues (see CMakeLists comments) — deferred.
- **FMV audio sync from BIN**: Works but incomplete; drop AVIs in `gamedata/fmv/` for proper sync.
- **pc_quicksave keyboard shortcuts**: F6/F8 quicksave/load are keyboard-only; on Switch these won't be bound. In-game save points work normally.
