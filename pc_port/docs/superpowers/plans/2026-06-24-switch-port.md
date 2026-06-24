# Switch Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and boot Silent Hill as a homebrew NRO on Nintendo Switch.

**Architecture:** The existing pc_port CMake build cross-compiles for aarch64 via devkitpro's `Switch.cmake` toolchain. PsyCross gets three targeted Switch guards (SDL include, GLES renderer selection, GLES offscreen readback fix). Two new platform files replace the PC entry point and map overlay loader. Everything else — SDL2, OpenAL, libjpeg, PsyCross GTE/SPU — is reused unchanged via devkitpro portlibs.

**Tech Stack:** devkitpro aarch64, libnx, SDL2 (portlib), OpenAL (portlib), libjpeg-turbo (portlib), OpenGL ES 3 via EGL, CMake 3.13+

## Global Constraints

- Branch: `switch-port` (off `mac-port-clean`) in `~/silent-hill-decomp`
- PsyCross changes go to `~/PsyCross` (root670 fork) — NOT `pc_port/PsyCross` (uninitialized SlickAmogus submodule)
- CMake finds `~/PsyCross` automatically via the `../../PsyCross` fallback in `pc_port/CMakeLists.txt`
- devkitpro installed at `/opt/devkitpro`; toolchain at `/opt/devkitpro/cmake/Switch.cmake`
- `NINTENDO_SWITCH=TRUE` and `__SWITCH__` are set automatically by the devkitpro toolchain — never set them manually
- Never add comments explaining what code does; only add a comment for a non-obvious WHY
- All debug output via `SH_DBG(...)`, never `fprintf(stderr, ...)`
- Game mechanics must match original PSX behavior exactly

---

### Task 1: Install portlibs and verify toolchain

**Files:**
- Create: `pc_port/build_switch.sh`

**Interfaces:**
- Produces: `build_switch/SilentHillPC.nro` (later tasks depend on this script)

- [ ] **Step 1: Add devkitpro tools to PATH**

```bash
export PATH="$PATH:/opt/devkitpro/devkitA64/bin:/opt/devkitpro/tools/bin"
aarch64-none-elf-gcc --version
```
Expected: prints `aarch64-none-elf-gcc (devkitA64 ...)` version line.

- [ ] **Step 2: Install Switch portlibs**

```bash
sudo dkp-pacman -Sy switch-sdl2 switch-openal-soft switch-libjpeg-turbo
```
Expected: pacman installs packages without error.

- [ ] **Step 3: Verify portlib libraries are present**

```bash
ls /opt/devkitpro/portlibs/switch/lib/libSDL2.a \
   /opt/devkitpro/portlibs/switch/lib/libopenal.a \
   /opt/devkitpro/portlibs/switch/lib/libjpeg.a
```
Expected: all three files listed.

- [ ] **Step 4: Write build_switch.sh**

Create `pc_port/build_switch.sh`:
```bash
#!/usr/bin/env bash
set -euo pipefail
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export PATH="$PATH:$DEVKITPRO/devkitA64/bin:$DEVKITPRO/tools/bin"
cmake -B build_switch \
  -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build_switch -j"$(nproc)"
```

```bash
chmod +x pc_port/build_switch.sh
```

- [ ] **Step 5: Smoke-test CMake configure (expect failure — sources not wired yet)**

```bash
cd pc_port
bash build_switch.sh 2>&1 | head -30
```
Expected: CMake configure succeeds and prints `-- PsyCross found at: ...` (the `../../PsyCross` fallback). The build step will fail on missing files — that's expected at this stage.

- [ ] **Step 6: Commit**

```bash
git -C ~/silent-hill-decomp add pc_port/build_switch.sh
git -C ~/silent-hill-decomp commit -m "switch: add build_switch.sh"
```

---

### Task 2: PsyCross Switch support

**Files:**
- Modify: `~/PsyCross/src/platform.h:18`
- Modify: `~/PsyCross/include/PsyX/PsyX_render.h:20`
- Modify: `~/PsyCross/src/render/PsyX_render.cpp:1956-1992`
- Modify: `~/PsyCross/CMakeLists.txt:29-41`

**Interfaces:**
- Produces: PsyCross builds for Switch with GLES 3 renderer; `RENDERER_OGLES` and `OGLES_VERSION=3` defined; `glad.c` excluded from Switch builds

- [ ] **Step 1: Add `__SWITCH__` to SDL include branch in platform.h**

In `~/PsyCross/src/platform.h`, line 18, change:
```c
#elif defined(_WINDOWS) || defined(__MINGW32__) || defined(__linux__) || defined(__ANDROID__) || defined(__RPI__)
#   include <SDL.h>
```
to:
```c
#elif defined(_WINDOWS) || defined(__MINGW32__) || defined(__linux__) || defined(__ANDROID__) || defined(__RPI__) || defined(__SWITCH__)
#   include <SDL.h>
```

- [ ] **Step 2: Add `__SWITCH__` renderer selection in PsyX_render.h**

In `~/PsyCross/include/PsyX/PsyX_render.h`, after the `__ANDROID__` block (currently ending at line 20), add:
```c
#elif defined(__ANDROID__)
#   define RENDERER_OGLES
#   define OGLES_VERSION (3)
#elif defined(__SWITCH__)
#   define RENDERER_OGLES
#   define OGLES_VERSION (3)
#endif
```
(The `#endif` was already there — just insert the two `__SWITCH__` lines before it.)

- [ ] **Step 3: Apply GLES offscreen readback fix in PsyX_render.cpp**

In `~/PsyCross/src/render/PsyX_render.cpp`, find `GR_SetOffscreenState`. Replace the block starting at `#if USE_OFFSCREEN_BLIT` (line ~1956) through the closing `}` of the inner block (line ~1992):

**Replace this:**
```cpp
#if USE_OFFSCREEN_BLIT
		// before drawing set source and target
		{
			glBindFramebuffer(GL_FRAMEBUFFER, g_glVRAMFramebuffer);

			// rebind texture
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_vramTexture, 0);

			// setup draw and read framebuffers
			glBindFramebuffer(GL_READ_FRAMEBUFFER, g_glOffscreenFramebuffer);					// source is backbuffer
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_glVRAMFramebuffer);

			glBlitFramebuffer(0, 0, g_PreviousOffscreen.w, g_PreviousOffscreen.h,
								g_PreviousOffscreen.x, g_PreviousOffscreen.y + g_PreviousOffscreen.h, g_PreviousOffscreen.x + g_PreviousOffscreen.w, g_PreviousOffscreen.y,
								GL_COLOR_BUFFER_BIT, GL_NEAREST);

			// done, unbind
			glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		}
#endif

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		// copy rendering results to VRAM texture
		{
			// reat the texture
			glBindTexture(GL_TEXTURE_2D, g_offscreenRTTexture);
			//glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
			PBO_Download(&g_glOffscreenPBO);
			glBindTexture(GL_TEXTURE_2D, g_lastBoundTexture);

			// Don't forcely update VRAM
			GR_CopyRGBAFramebufferToVRAM((u_int*)g_glOffscreenPBO.pixels,
				g_PreviousOffscreen.x, g_PreviousOffscreen.y, g_PreviousOffscreen.w, g_PreviousOffscreen.h,
				USE_OFFSCREEN_BLIT == 0, 1);
		}
```

**With this:**
```cpp
#if USE_OFFSCREEN_BLIT && !defined(RENDERER_OGLES)
		// before drawing set source and target
		{
			glBindFramebuffer(GL_FRAMEBUFFER, g_glVRAMFramebuffer);

			// rebind texture
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_vramTexture, 0);

			// setup draw and read framebuffers
			glBindFramebuffer(GL_READ_FRAMEBUFFER, g_glOffscreenFramebuffer);					// source is backbuffer
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_glVRAMFramebuffer);

			glBlitFramebuffer(0, 0, g_PreviousOffscreen.w, g_PreviousOffscreen.h,
								g_PreviousOffscreen.x, g_PreviousOffscreen.y + g_PreviousOffscreen.h, g_PreviousOffscreen.x + g_PreviousOffscreen.w, g_PreviousOffscreen.y,
								GL_COLOR_BUFFER_BIT, GL_NEAREST);

			// done, unbind
			glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		}
#endif

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		{
#if defined(RENDERER_OGLES)
			// GLES has no glGetTexImage — read back via glReadPixels instead.
			glBindFramebuffer(GL_FRAMEBUFFER, g_glOffscreenFramebuffer);
#if USE_PBO
			glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
#endif
			glReadPixels(0, 0, g_PreviousOffscreen.w, g_PreviousOffscreen.h, GL_RGBA, GL_UNSIGNED_BYTE, g_glOffscreenPBO.pixels);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			GR_CopyRGBAFramebufferToVRAM((u_int*)g_glOffscreenPBO.pixels,
				g_PreviousOffscreen.x, g_PreviousOffscreen.y,
				g_PreviousOffscreen.w, g_PreviousOffscreen.h,
				USE_OFFSCREEN_BLIT == 0, 1);

#if OGLES_VERSION == 3
			glBindTexture(GL_TEXTURE_2D, g_vramTexture);
			glPixelStorei(GL_UNPACK_ROW_LENGTH, VRAM_WIDTH);
			glTexSubImage2D(GL_TEXTURE_2D, 0,
				g_PreviousOffscreen.x, g_PreviousOffscreen.y,
				g_PreviousOffscreen.w, g_PreviousOffscreen.h,
				VRAM_FORMAT, GL_UNSIGNED_BYTE,
				vram + g_PreviousOffscreen.x + g_PreviousOffscreen.y * VRAM_WIDTH);
			glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
			glBindTexture(GL_TEXTURE_2D, g_lastBoundTexture);
#else
			vram_need_update = 1;
#endif
#else
			glBindTexture(GL_TEXTURE_2D, g_offscreenRTTexture);
			//glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
			PBO_Download(&g_glOffscreenPBO);
			glBindTexture(GL_TEXTURE_2D, g_lastBoundTexture);

			GR_CopyRGBAFramebufferToVRAM((u_int*)g_glOffscreenPBO.pixels,
				g_PreviousOffscreen.x, g_PreviousOffscreen.y, g_PreviousOffscreen.w, g_PreviousOffscreen.h,
				USE_OFFSCREEN_BLIT == 0, 1);
#endif
		}
```

- [ ] **Step 4: Update PsyCross CMakeLists.txt for Switch**

In `~/PsyCross/CMakeLists.txt`, replace:
```cmake
find_package(SDL2 REQUIRED)
target_link_libraries(psycross_static ${SDL2_LIBRARIES})
target_include_directories(psycross_static PRIVATE ${SDL2_INCLUDE_DIRS})

find_package(OPENAL REQUIRED)
target_link_libraries(psycross_static OpenAL)
# Guard against empty OpenAL_SOURCE_DIR (system framework on macOS) expanding to /include
if(OpenAL_SOURCE_DIR)
    target_include_directories(psycross_static PRIVATE ${OpenAL_BINARY_DIR} ${OpenAL_SOURCE_DIR}/include)
endif()
if(OPENAL_INCLUDE_DIR)
    target_include_directories(psycross_static PRIVATE ${OPENAL_INCLUDE_DIR})
endif()
```
with:
```cmake
if(NINTENDO_SWITCH)
    # SDL2 and OpenAL come from portlibs; pkg-config is handled by Switch.cmake toolchain
    target_include_directories(psycross_static PRIVATE
        "$ENV{DEVKITPRO}/portlibs/switch/include"
        "$ENV{DEVKITPRO}/portlibs/switch/include/SDL2"
        "$ENV{DEVKITPRO}/libnx/include"
    )
    # Exclude glad.c — GLES headers come from portlibs/EGL, no GL loader needed
    get_target_property(_psycross_srcs psycross_static SOURCES)
    list(FILTER _psycross_srcs EXCLUDE REGEX "glad\\.c$")
    set_target_properties(psycross_static PROPERTIES SOURCES "${_psycross_srcs}")
else()
    find_package(SDL2 REQUIRED)
    target_link_libraries(psycross_static ${SDL2_LIBRARIES})
    target_include_directories(psycross_static PRIVATE ${SDL2_INCLUDE_DIRS})

    find_package(OPENAL REQUIRED)
    target_link_libraries(psycross_static OpenAL)
    if(OpenAL_SOURCE_DIR)
        target_include_directories(psycross_static PRIVATE ${OpenAL_BINARY_DIR} ${OpenAL_SOURCE_DIR}/include)
    endif()
    if(OPENAL_INCLUDE_DIR)
        target_include_directories(psycross_static PRIVATE ${OPENAL_INCLUDE_DIR})
    endif()
endif()
```

- [ ] **Step 5: Commit PsyCross changes**

```bash
git -C ~/PsyCross add src/platform.h include/PsyX/PsyX_render.h src/render/PsyX_render.cpp CMakeLists.txt
git -C ~/PsyCross commit -m "switch: add __SWITCH__ support (SDL include, GLES renderer, offscreen readback fix)"
```

---

### Task 3: New Switch platform files

**Files:**
- Create: `pc_port/switch/main_switch.c`
- Create: `pc_port/switch/map_switch.c`
- Copy: `pc_port/switch/icon.jpg` (from REDRIVER2 as placeholder)

**Interfaces:**
- Consumes: `PsxMemory_Init()`, `PsyX_Initialise()`, `MainLoop()`, `FsPC_Init()`, `PsyX_Log_SetStream()`, all anim info builders (same extern pattern as `main_pc.c`)
- Produces: `g_pMapOverlayHeader` pointing at `g_MapOverlayHeader_map0_s00`; `main()` entry point for Switch

- [ ] **Step 1: Copy placeholder icon**

```bash
cp ~/REDRIVER2-switch/switch/icon.jpg ~/silent-hill-decomp/pc_port/switch/icon.jpg
```

- [ ] **Step 2: Create map_switch.c**

Create `pc_port/switch/map_switch.c`:
```c
#include "common.h"
#include "game.h"
#include "bodyprog/map/map.h"

extern s_MapOverlayHdr g_MapOverlayHeader_map0_s00;
s_MapOverlayHdr* g_pMapOverlayHeader = &g_MapOverlayHeader_map0_s00;
```

- [ ] **Step 3: Create main_switch.c**

Create `pc_port/switch/main_switch.c`:
```c
#include <switch.h>
#include <switch/runtime/nxlink.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include "fs_pc.h"
#include "pc_config.h"
#include "psx_memory.h"
#include "sh_log.h"

#include <PsyX/PsyX_public.h>

/* nxlink socket for stdio redirection */
static int s_nxlinkSock = -1;
static bool s_socketInit = false;

static void InitNxlink(void)
{
    if (R_FAILED(socketInitializeDefault()))
        return;
    s_socketInit = true;
    s_nxlinkSock = nxlinkStdio();
}

static void ExitNxlink(void)
{
    if (s_nxlinkSock >= 0) { close(s_nxlinkSock); s_nxlinkSock = -1; }
    if (s_socketInit)      { socketExit(); s_socketInit = false; }
}

static void EnsureDir(const char* path)
{
    char buf[256];
    size_t len = strlen(path);
    if (len >= sizeof(buf)) return;
    for (size_t i = 0; i < len; i++) {
        buf[i] = path[i];
        buf[i+1] = '\0';
        if (path[i] == '/' && i > 0 && path[i-1] != ':')
            mkdir(buf, 0777);
    }
    mkdir(path, 0777);
}

static void ShowError(const char* title, const char* msg)
{
    consoleInit(NULL);
    consoleClear();
    printf("%s\n\n%s\n\nThe app will close in 20 seconds.\n", title, msg);
    consoleUpdate(NULL);
    sleep(20);
    consoleExit(NULL);
}

/* Locate the disc image in gamedata/ and initialise CDFS + file table.
 * PcPort_GetGameDiscPath() / FindAndOpenDiscImage() are static in main_pc.c
 * and unavailable here, so we replicate the minimal detection logic. */
extern void Fs_InitFileTableForRegion(int region);
extern void PsyX_CDFS_Init(const char* path, int unk1, int unk2);

enum { Region_USA = 0, Region_EUR = 1 };

static void InitDisc(void)
{
    static const struct { const char* name; int region; } s_known[] = {
        { "gamedata/Silent Hill (USA).bin", Region_USA },
        { "gamedata/Silent Hill (PAL).bin", Region_EUR },
        { "gamedata/Silent Hill (Europe) (En,Fr,De,Es,It).bin", Region_EUR },
    };
    int n = (int)(sizeof(s_known) / sizeof(s_known[0]));
    for (int i = 0; i < n; i++) {
        FILE* f = fopen(s_known[i].name, "rb");
        if (f) {
            fclose(f);
            Fs_InitFileTableForRegion(s_known[i].region);
            PsyX_CDFS_Init(s_known[i].name, 0, 0);
            SH_LOG("Disc: %s", s_known[i].name);
            return;
        }
    }
    /* Fallback: scan gamedata/ for any .bin */
    DIR* d = opendir("gamedata");
    if (d) {
        struct dirent* ent;
        while ((ent = readdir(d)) != NULL) {
            size_t l = strlen(ent->d_name);
            if (l > 4 && strcmp(ent->d_name + l - 4, ".bin") == 0) {
                char path[256];
                snprintf(path, sizeof(path), "gamedata/%s", ent->d_name);
                closedir(d);
                Fs_InitFileTableForRegion(Region_USA);
                PsyX_CDFS_Init(path, 0, 0);
                SH_LOG("Disc (autodetect): %s", path);
                return;
            }
        }
        closedir(d);
    }
    SH_WARN("No disc image found in gamedata/ — assets will not load");
}

/* All anim info builders — same list as main_pc.c */
extern void PcPort_InitCharaAnimInfo(void);
extern void PcPort_InitSdBuffers(void);
extern void AsRodata_Reformat(void);
extern void GroanerAnimInfos_Init(void);
extern void BloodsuckerAnimInfos_Init(void);
extern void BloodyLisaAnimInfos_Init(void);
extern void AlessaAnimInfos_Init(void);
extern void GhostChildAlessaAnimInfos_Init(void);
extern void LisaAnimInfos_Init(void);
extern void KaufmannAnimInfos_Init(void);
extern void DahliaAnimInfos_Init(void);
extern void CatAnimInfos_Init(void);
extern void PuppetNurseData_Init(void);
extern void LarvalStalkerAnimInfos_Init(void);
extern void HangedScratcherAnimInfos_Init(void);
extern void CreeperAnimInfos_Init(void);
extern void SplitHeadAnimInfos_Init(void);
extern void RomperAnimInfos_Init(void);
extern void LockerDeadBodyAnimInfos_Init(void);
extern void TwinfeelerAnimInfos_Init(void);
extern void FloatstingerAnimInfos_Init(void);
extern void MonsterCybilAnimInfos_Init(void);
extern void FlaurosAnimInfos_Init(void);
extern void ParasiteAnimInfos_Init(void);
extern void GhostDoctorAnimInfos_Init(void);
extern void BloodyIncubatorAnimInfos_Init(void);
extern void IncubatorAnimInfos_Init(void);
extern void LittleIncubusAnimInfos_Init(void);
extern void IncubusAnimInfos_Init(void);
extern void Unkkown23AnimInfos_Init(void);
extern void Map6S04ExtraAnimInfos_Init(void);
extern void CharaData_ApplyRegionPatches(void);

extern void* g_OvlDynamic;
extern void* g_OvlBodyprog;
typedef struct s_DemoFrameData s_DemoFrameData;
extern s_DemoFrameData* g_Demo_PlayFileBufferPtr;

extern void MainLoop(void);
extern void ResetCallback(void);
extern void ResetGraph(int);
extern void SetGraphDebug(int);
extern void SpuInit(void);
extern void CdInit(void);
extern void Fs_QueueInitialize(void);
extern void MapRegistry_Init(void);

int main(int argc, char** argv)
{
    InitNxlink();

    /* Route all SH_DBG / PsyCross log output through stdout to nxlink */
    PsyX_Log_SetStream(stdout);

    EnsureDir("sdmc:/switch/SilentHill");
    if (chdir("sdmc:/switch/SilentHill") != 0)
    {
        ShowError("Fatal", "Cannot access sdmc:/switch/SilentHill\nCheck SD card.");
        ExitNxlink();
        return 1;
    }

    FsPC_Init("gamedata");
    PcConfig_Load("config.cfg");

    PsxMemory_Init();

    PcPort_InitCharaAnimInfo();
    PcPort_InitSdBuffers();
    AsRodata_Reformat();

    GroanerAnimInfos_Init();
    BloodsuckerAnimInfos_Init();
    BloodyLisaAnimInfos_Init();
    AlessaAnimInfos_Init();
    GhostChildAlessaAnimInfos_Init();
    LisaAnimInfos_Init();
    KaufmannAnimInfos_Init();
    DahliaAnimInfos_Init();
    CatAnimInfos_Init();
    PuppetNurseData_Init();
    LarvalStalkerAnimInfos_Init();
    HangedScratcherAnimInfos_Init();
    CreeperAnimInfos_Init();
    SplitHeadAnimInfos_Init();
    RomperAnimInfos_Init();
    LockerDeadBodyAnimInfos_Init();
    TwinfeelerAnimInfos_Init();
    FloatstingerAnimInfos_Init();
    MonsterCybilAnimInfos_Init();
    FlaurosAnimInfos_Init();
    ParasiteAnimInfos_Init();
    GhostDoctorAnimInfos_Init();
    BloodyIncubatorAnimInfos_Init();
    IncubatorAnimInfos_Init();
    LittleIncubusAnimInfos_Init();
    IncubusAnimInfos_Init();
    Unkkown23AnimInfos_Init();
    Map6S04ExtraAnimInfos_Init();

    {
        extern void* D_800ED230[2];
        D_800ED230[0] = FS_BUFFER_20;
        D_800ED230[1] = FS_BUFFER_18;
    }

    g_OvlDynamic  = PSX_ADDR(0x000C9578);
    g_OvlBodyprog = PSX_ADDR(0x00024B60);
    g_Demo_PlayFileBufferPtr = (s_DemoFrameData*)PSX_ADDR(0x000F5E00);

    InitDisc();

    PsyX_Initialise("Silent Hill", 1280, 720, 0);

    CharaData_ApplyRegionPatches();

    ResetCallback();
    SpuInit();
    CdInit();
    ResetGraph(0);
    SetGraphDebug(0);
    Fs_QueueInitialize();
    MapRegistry_Init();

    SH_LOG("Switch: entering MainLoop");
    MainLoop();

    PsyX_Shutdown();
    ExitNxlink();
    return 0;
}
```

- [ ] **Step 4: Commit**

```bash
git -C ~/silent-hill-decomp add pc_port/switch/
git -C ~/silent-hill-decomp commit -m "switch: add platform files (main_switch.c, map_switch.c, icon)"
```

---

### Task 4: pc_port CMakeLists Switch additions + FMV fix

**Files:**
- Modify: `pc_port/CMakeLists.txt`
- Modify: `pc_port/src/fmv/fmv_player.cpp:24`

**Interfaces:**
- Produces: CMake configure succeeds for Switch; sources correctly excluded/included; NRO built as post-build step

- [ ] **Step 1: Add source exclusions for Switch after the existing mac-port-clean pc_crash exclusion**

In `pc_port/CMakeLists.txt`, after the existing block (lines 158–162):
```cmake
# pc_crash.c is Windows-only (SEH crash telemetry); exclude on non-Windows platforms.
# dll_loader.c and fmv_player.cpp both have #ifdef _WIN32 guards and work on POSIX.
if(NOT WIN32)
    list(FILTER PC_PORT_SOURCES EXCLUDE REGEX "pc_crash\\.c$")
endif()
```

Add immediately after:
```cmake
if(NINTENDO_SWITCH)
    # main_pc.c replaced by switch/main_switch.c; map overlay loader replaced by
    # switch/map_switch.c (static pointer — no dlopen on Switch).
    list(FILTER PC_PORT_SOURCES EXCLUDE REGEX "main_pc\\.c$")
    list(FILTER PC_PORT_SOURCES EXCLUDE REGEX "map_overlay_loader\\.c$")
    list(FILTER PC_PORT_SOURCES EXCLUDE REGEX "dll_loader\\.c$")

    set(SWITCH_SOURCES
        "${CMAKE_SOURCE_DIR}/switch/main_switch.c"
        "${CMAKE_SOURCE_DIR}/switch/map_switch.c"
    )
endif()
```

- [ ] **Step 2: Add Switch sources to the executable**

In `pc_port/CMakeLists.txt`, change `add_executable(SilentHillPC ...)` (line 181) to include `${SWITCH_SOURCES}`:
```cmake
add_executable(SilentHillPC
    ${PC_PORT_SOURCES}
    ${MAIN_SOURCES}
    ${BODYPROG_SOURCES}
    ${SCREEN_SOURCES}
    ${MAP_SOURCES}
    ${SH_APP_RESOURCES}
    ${SWITCH_SOURCES}
)
```

- [ ] **Step 3: Disable ENABLE_EXPORTS on Switch**

In `pc_port/CMakeLists.txt`, change the ENABLE_EXPORTS line (line 194) from:
```cmake
set_target_properties(SilentHillPC PROPERTIES ENABLE_EXPORTS ON)
```
to:
```cmake
if(NOT NINTENDO_SWITCH)
    set_target_properties(SilentHillPC PROPERTIES ENABLE_EXPORTS ON)
endif()
if(MINGW)
    target_link_options(SilentHillPC PRIVATE "-Wl,--export-all-symbols")
    target_link_options(SilentHillPC PRIVATE "-Wl,--subsystem,windows" "-Wl,-emainCRTStartup")
endif()
```
(Remove the existing `if(MINGW)` block that immediately followed — it's now absorbed above.)

- [ ] **Step 4: Add Switch linker libraries**

In `pc_port/CMakeLists.txt`, after the existing `if(WIN32) ... elseif(UNIX)` OpenGL linking block (around line 294–299), add:
```cmake
if(NINTENDO_SWITCH)
    target_link_libraries(SilentHillPC PRIVATE EGL GLESv2 glapi drm_nouveau nx m)
    target_include_directories(SilentHillPC PRIVATE
        "$ENV{DEVKITPRO}/portlibs/switch/include"
        "$ENV{DEVKITPRO}/portlibs/switch/include/SDL2"
        "$ENV{DEVKITPRO}/libnx/include"
    )
endif()
```

- [ ] **Step 5: Add Switch compiler flags**

In `pc_port/CMakeLists.txt`, in the `else()` branch of the compiler flags section (after the `if(APPLE)` block, around line 331), add:
```cmake
    if(NINTENDO_SWITCH)
        target_compile_options(SilentHillPC PRIVATE
            -Wno-implicit-function-declaration
            -Wno-ignored-qualifiers
            -Wno-int-conversion
            -Wno-return-mismatch
            -Wno-narrowing
            -Wno-write-strings
            -fsigned-char
            -fpermissive
        )
    endif()
```

- [ ] **Step 6: Add NRO packaging post-build step**

In `pc_port/CMakeLists.txt`, after the `if(NOT EXISTS ... config.cfg)` block (around line 338), add:
```cmake
if(NINTENDO_SWITCH)
    nx_create_nro(SilentHillPC ICON "${CMAKE_SOURCE_DIR}/switch/icon.jpg")
endif()
```

- [ ] **Step 7: Fix fmv_player.cpp glad.h include**

In `pc_port/src/fmv/fmv_player.cpp`, change line 24 from:
```cpp
#include <PsyX/common/glad.h>
```
to:
```cpp
#if defined(RENDERER_OGL)
#include <PsyX/common/glad.h>
#endif
```

- [ ] **Step 8: Commit**

```bash
git -C ~/silent-hill-decomp add pc_port/CMakeLists.txt pc_port/src/fmv/fmv_player.cpp
git -C ~/silent-hill-decomp commit -m "switch: CMakeLists exclusions, linker, NRO packaging; fmv glad.h guard"
```

---

### Task 5: First build — fix compilation errors

**Files:**
- Modify: whichever files produce errors (expect aarch64-specific type issues, missing includes, undefined symbols)

**Interfaces:**
- Produces: `build_switch/SilentHillPC.nro` exists and is a valid NRO

- [ ] **Step 1: Run the build and capture output**

```bash
cd ~/silent-hill-decomp/pc_port
bash build_switch.sh 2>&1 | tee /tmp/switch_build.log
```

- [ ] **Step 2: Check if NRO was produced**

```bash
ls -lh build_switch/SilentHillPC.nro 2>/dev/null || echo "BUILD FAILED — see /tmp/switch_build.log"
```

- [ ] **Step 3: Triage errors**

If build fails, check the first error:
```bash
grep -m 5 "error:" /tmp/switch_build.log
```

Common expected errors and fixes:

**`error: 'MapRegistry_Init' undeclared`** — `main_switch.c` needs `MapRegistry_Init()` before `MainLoop()`. Add to the init sequence in `switch/main_switch.c`:
```c
extern void MapRegistry_Init(void);
// add after Fs_QueueInitialize():
MapRegistry_Init();
```

**`error: redefinition of 'g_pMapOverlayHeader'`** — `map_overlay_loader.c` wasn't excluded. Verify the `EXCLUDE REGEX "map_overlay_loader\\.c$"` filter is in place and re-run `cmake -B build_switch` (not just `--build`).

**`error: 'SpuInit' undeclared in main_switch.c`** — add `#include <psx/libspu.h>` to `switch/main_switch.c`.

**`fatal error: 'switch.h' file not found`** — devkitpro include path missing. Verify `DEVKITPRO` env var is set and `/opt/devkitpro/libnx/include` contains `switch.h`.

- [ ] **Step 4: Iterate until build succeeds**

Fix each error, rebuild:
```bash
cmake --build build_switch -j"$(nproc)" 2>&1 | grep -E "error:|warning:" | head -20
```
Repeat until `build_switch/SilentHillPC.nro` exists.

- [ ] **Step 5: Commit fixes**

```bash
git -C ~/silent-hill-decomp add -p   # stage only the fix hunks
git -C ~/silent-hill-decomp commit -m "switch: fix build errors"
# if PsyCross fixes were needed:
git -C ~/PsyCross add -p
git -C ~/PsyCross commit -m "switch: fix build errors"
```

---

### Task 6: Boot on Switch via nxlink

**Files:** (none — deployment only)

**Interfaces:**
- Produces: Game boots to the Konami logo on Switch hardware; SH_DBG output visible in terminal via nxlink

- [ ] **Step 1: Copy NRO to SD card**

Either via USB or directly:
```bash
# Via nxlink (send and launch):
nxlink -a <switch_ip> ~/silent-hill-decomp/pc_port/build_switch/SilentHillPC.nro
```

Or copy manually to `sdmc:/switch/SilentHill/SilentHill.nro` and launch from hbmenu.

- [ ] **Step 2: Ensure game data is in place on SD card**

```
sdmc:/switch/SilentHill/gamedata/Silent Hill (USA).bin
```
(The `.bin` must be present; saves and config are auto-created.)

- [ ] **Step 3: Attach stdio relay and watch output**

If launched manually from hbmenu:
```bash
nxlink -p <switch_ip>
```
Expected output in terminal:
```
[SH] Switch: entering MainLoop
```

- [ ] **Step 4: Verify boot**

Expected: Game reaches the Konami logo or main menu without crashing.

If the Switch crashes / hangs: check the nxlink terminal output for the last `SH_DBG` line before the crash. Common first-boot issues:
- **Blank screen, no output**: nxlink socket not connecting — confirm Switch IP, retry `nxlink -p`
- **`FsPC: Failed to open`**: disc image not found — check `sdmc:/switch/SilentHill/gamedata/` contains the `.bin`
- **Crash in `PsyX_Initialise`**: GLES context failure — confirm `switch-sdl2` portlib is installed

- [ ] **Step 5: Commit final state**

```bash
git -C ~/silent-hill-decomp add -A
git -C ~/silent-hill-decomp commit -m "switch: working boot (Task 6 complete)"
```
