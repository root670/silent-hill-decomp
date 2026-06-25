#include <switch.h>
#include <switch/runtime/nxlink.h>
#include <switch/applets/error.h>
#include <switch/services/nv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <SDL.h>

#include "fs_pc.h"
#include "pc_config.h"
#include "psx_memory.h"
#include "sh_log.h"
#include "main/fsqueue.h"
#include "dbg_overlay.h"

/* Shared map helper function — body in the header, compiled here as on PC.
 * Forward-declare the dependencies to avoid pulling in bodyprog.h. */
typedef int q19_12;
extern q19_12 g_DeltaTime;
extern void SysWork_StateStepIncrement(int);

static inline q19_12 Q12_MULT_PRECISE_SW(q19_12 a, q19_12 b) {
    return (q19_12)(((long long)a * b) >> 12);
}
#define Q12_MULT_PRECISE(a, b) Q12_MULT_PRECISE_SW(a, b)

void SysWork_StateStepIncrementAfterTime(q19_12* timer, q19_12 timeInc,
    q19_12 timeMin, q19_12 timeMax, int setTimerToMax, int incStateStep)
{
    if (*timer < timeMin) { *timer = timeMin; return; }
    *timer += Q12_MULT_PRECISE(g_DeltaTime, timeInc);
    if (*timer > timeMax) {
        if (setTimerToMax) *timer = timeMax;
        if (incStateStep) SysWork_StateStepIncrement(0);
    }
}
#undef Q12_MULT_PRECISE

#include <PsyX/PsyX_public.h>

/* Globals that main_pc.c provides on other platforms */
FILE* g_ShDebugLog         = NULL;
int   g_ShDebugEchoStdout  = 0;
void (*g_ShOverlayPushLine)(const char* line) = NULL;
int   g_PcAllowDebugControls = 0;

static int s_nxlinkSock = -1;
static bool s_socketInit = false;

/* GPU load monitoring via nvhost-ctrl-gpu PMU ioctl.
 * Returns tenths-of-percent (0-1000). Returns 0 if unavailable. */
#define NVGPU_GPU_IOCTL_PMU_GET_GPU_LOAD 0x80044715
static u32 s_nvGpuFd = (u32)-1;
static bool s_nvInit = false;
/* Smoothed GPU load (tenths of percent, 0-1000). */
static u32 g_gpuLoad10 = 0;

/* Perf counters from PsyX_GPU.cpp — read-only here. */
extern int g_perf_splits3d;
extern int g_perf_verts3d;
extern int g_perf_splits2d;
extern int g_perf_verts2d;
extern float g_perf_submit_ms;

/* Called from DrawAllSplits every ~180 calls (~3s) on the main thread. */
extern void (*g_perf_callback)(int s3d, int v3d, int s2d, int v2d, float ms);

static FILE* s_perfLog = NULL;

static void PerfCallback(int s3d, int v3d, int s2d, int v2d, float ms)
{
    /* Sample GPU load on the main thread — safe, no threading needed. */
    if (s_nvGpuFd != (u32)-1) {
        u32 load = 0;
        nvIoctl(s_nvGpuFd, NVGPU_GPU_IOCTL_PMU_GET_GPU_LOAD, &load);
        g_gpuLoad10 = (g_gpuLoad10 * 7 + load * 3) / 10;
    }

    u32 gload = g_gpuLoad10;
    char buf[160];
    int n = snprintf(buf, sizeof(buf),
        "[PERF] GPU=%u.%u%%  submit=%.2fms"
        "  3D: %d splits %d verts  2D: %d splits %d verts\n",
        gload / 10, gload % 10, (double)ms,
        s3d, v3d, s2d, v2d);
    fwrite(buf, 1, n, stdout);
    fflush(stdout);
    if (s_perfLog) { fwrite(buf, 1, n, s_perfLog); fflush(s_perfLog); }
}

static void InitNvGpu(void)
{
    if (R_SUCCEEDED(nvInitialize())) {
        s_nvInit = true;
        if (R_FAILED(nvOpen(&s_nvGpuFd, "/dev/nvhost-ctrl-gpu")))
            s_nvGpuFd = (u32)-1;
    }
}

static void ExitNvGpu(void)
{
    g_perf_callback = NULL;
    if (s_perfLog) { fclose(s_perfLog); s_perfLog = NULL; }
    if (s_nvGpuFd != (u32)-1) { nvClose(s_nvGpuFd); s_nvGpuFd = (u32)-1; }
    if (s_nvInit) { nvExit(); s_nvInit = false; }
}

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
    ErrorApplicationConfig cfg;
    errorApplicationCreate(&cfg, title, msg);
    errorApplicationShow(&cfg);
}

extern void PsyX_CDFS_Init(const char* path, int unk1, int unk2);

static char s_discPath[512] = "";

static int InitDisc(void)
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
            strncpy(s_discPath, s_known[i].name, sizeof(s_discPath) - 1);
            Fs_InitFileTableForRegion(s_known[i].region);
            PsyX_CDFS_Init(s_known[i].name, 0, 0);
            SH_LOG("Disc: %s", s_known[i].name);
            return 0;
        }
    }
    DIR* d = opendir("gamedata");
    if (d) {
        struct dirent* ent;
        while ((ent = readdir(d)) != NULL) {
            size_t l = strlen(ent->d_name);
            if (l > 4 && strcmp(ent->d_name + l - 4, ".bin") == 0) {
                char path[256];
                snprintf(path, sizeof(path), "gamedata/%s", ent->d_name);
                closedir(d);
                strncpy(s_discPath, path, sizeof(s_discPath) - 1);
                Fs_InitFileTableForRegion(Region_USA);
                PsyX_CDFS_Init(path, 0, 0);
                SH_LOG("Disc (autodetect): %s", path);
                return 0;
            }
        }
        closedir(d);
    }
    return -1;
}

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
extern void SpuInit(void);
extern void CdInit(void);
extern void Fs_QueueInitialize(void);
extern void MapRegistry_Init(void);

const char* PcPort_GetGameDataPath(void) { return "gamedata"; }

const char* PcPort_GetGameDiscPath(void) { return s_discPath; }

int main(int argc, char** argv)
{
    InitNxlink();
    InitNvGpu();

    /* PsyCross already printf()s to stdout unconditionally — no SetStream needed.
     * g_ShDebugLog stays NULL so SH_DBG is a no-op; per-frame debug spam over
     * the nxlink TCP socket saturates a CPU core. Enable via enable_debug_log=1
     * in config.cfg to get a SilentHill.log on the SD card instead. */

    EnsureDir("sdmc:/switch/SilentHill");
    if (chdir("sdmc:/switch/SilentHill") != 0)
    {
        ShowError("Fatal", "Cannot access sdmc:/switch/SilentHill\nCheck SD card.");
        ExitNxlink();
        return 1;
    }

    FsPC_Init("gamedata");
    PcConfig_Load("config.cfg");

    /* Enable vsync so the Tegra GPU power management can idle between frames.
     * Without vsync the GPU has no idle signal and runs at full power even on
     * simple 2D screens. Our VBlank semaphore in PsyX_WaitForTimestep still
     * caps frame rate; vsync adds proper GPU power management on top. */
    g_cfg_swapInterval = 1;

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

    if (InitDisc() != 0)
    {
        ShowError("Disc image not found",
            "Place your Silent Hill disc image (.bin) in:\n"
            "sdmc:/switch/SilentHill/gamedata/\n\n"
            "Example: Silent Hill (USA).bin");
        ExitNxlink();
        return 1;
    }

    /* Force NTSC (60Hz) — g_vmode defaults to -1 and the decomp never calls
     * SetVideoMode, so the interrupt thread would otherwise use PAL timing. */
    SetVideoMode(0);

    /* Match output resolution to display mode: 1080p docked, 720p handheld. */
    int resW, resH;
    if (appletGetOperationMode() == AppletOperationMode_Console) {
        /* TEMPORARY: Task 0a profiling — force 720p docked to measure fragment-bound cost */
        resW = 1280; resH = 720;
    } else {
        resW = 1280; resH = 720;
    }

    PsyX_Initialise("Silent Hill", resW, resH, 0);

    s_perfLog = fopen("perf.log", "w");
    g_perf_callback = PerfCallback;

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
    ExitNvGpu();
    ExitNxlink();
    return 0;
}
