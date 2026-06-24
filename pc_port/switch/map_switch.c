/*
 * map_switch.c — Static map overlay registry for the Switch port.
 *
 * All 43 map overlays are compiled and linked statically. Each map's internal
 * symbols are localized via objcopy --keep-global-symbol at build time so only
 * g_MapOverlayHeader_<name> is visible here. MapOverlay_Load looks up the
 * requested map by name and returns its header pointer.
 *
 * Maps not yet available at build time fall back gracefully (return NULL).
 */
#include "common.h"
#include "game.h"
#include "bodyprog/map/map.h"
#include "map_registry.h"
#include <string.h>

/* Weak declarations — maps compiled with objcopy-sealed objects provide the
 * real definitions; maps not yet compiled resolve to NULL via the weak fallback. */
#define DECLARE_MAP(name) \
    extern s_MapOverlayHdr g_MapOverlayHeader_##name __attribute__((weak));

DECLARE_MAP(map0_s00) DECLARE_MAP(map0_s01) DECLARE_MAP(map0_s02)
DECLARE_MAP(map1_s00) DECLARE_MAP(map1_s01) DECLARE_MAP(map1_s02)
DECLARE_MAP(map1_s03) DECLARE_MAP(map1_s04) DECLARE_MAP(map1_s05)
DECLARE_MAP(map1_s06)
DECLARE_MAP(map2_s00) DECLARE_MAP(map2_s01) DECLARE_MAP(map2_s02)
DECLARE_MAP(map2_s03) DECLARE_MAP(map2_s04)
DECLARE_MAP(map3_s00) DECLARE_MAP(map3_s01) DECLARE_MAP(map3_s02)
DECLARE_MAP(map3_s03) DECLARE_MAP(map3_s04) DECLARE_MAP(map3_s05)
DECLARE_MAP(map3_s06)
DECLARE_MAP(map4_s00) DECLARE_MAP(map4_s01) DECLARE_MAP(map4_s02)
DECLARE_MAP(map4_s03) DECLARE_MAP(map4_s04) DECLARE_MAP(map4_s05)
DECLARE_MAP(map4_s06)
DECLARE_MAP(map5_s00) DECLARE_MAP(map5_s01) DECLARE_MAP(map5_s02)
DECLARE_MAP(map5_s03)
DECLARE_MAP(map6_s00) DECLARE_MAP(map6_s01) DECLARE_MAP(map6_s02)
DECLARE_MAP(map6_s03) DECLARE_MAP(map6_s04) DECLARE_MAP(map6_s05)
DECLARE_MAP(map7_s00) DECLARE_MAP(map7_s01) DECLARE_MAP(map7_s02)
DECLARE_MAP(map7_s03)

typedef struct {
    const char*      name;
    s_MapOverlayHdr* header;
} MapEntry;

#define MAP_ENTRY(name) { #name, &g_MapOverlayHeader_##name }

static MapEntry s_mapRegistry[] = {
    MAP_ENTRY(map0_s00), MAP_ENTRY(map0_s01), MAP_ENTRY(map0_s02),
    MAP_ENTRY(map1_s00), MAP_ENTRY(map1_s01), MAP_ENTRY(map1_s02),
    MAP_ENTRY(map1_s03), MAP_ENTRY(map1_s04), MAP_ENTRY(map1_s05),
    MAP_ENTRY(map1_s06),
    MAP_ENTRY(map2_s00), MAP_ENTRY(map2_s01), MAP_ENTRY(map2_s02),
    MAP_ENTRY(map2_s03), MAP_ENTRY(map2_s04),
    MAP_ENTRY(map3_s00), MAP_ENTRY(map3_s01), MAP_ENTRY(map3_s02),
    MAP_ENTRY(map3_s03), MAP_ENTRY(map3_s04), MAP_ENTRY(map3_s05),
    MAP_ENTRY(map3_s06),
    MAP_ENTRY(map4_s00), MAP_ENTRY(map4_s01), MAP_ENTRY(map4_s02),
    MAP_ENTRY(map4_s03), MAP_ENTRY(map4_s04), MAP_ENTRY(map4_s05),
    MAP_ENTRY(map4_s06),
    MAP_ENTRY(map5_s00), MAP_ENTRY(map5_s01), MAP_ENTRY(map5_s02),
    MAP_ENTRY(map5_s03),
    MAP_ENTRY(map6_s00), MAP_ENTRY(map6_s01), MAP_ENTRY(map6_s02),
    MAP_ENTRY(map6_s03), MAP_ENTRY(map6_s04), MAP_ENTRY(map6_s05),
    MAP_ENTRY(map7_s00), MAP_ENTRY(map7_s01), MAP_ENTRY(map7_s02),
    MAP_ENTRY(map7_s03),
};

#define REGISTRY_COUNT ((int)(sizeof(s_mapRegistry)/sizeof(s_mapRegistry[0])))

static s_MapOverlayHdr* s_currentHeader = NULL;
static char             s_currentName[32] = {0};

/* g_pMapOverlayHeader is defined in map_registry.c */
extern s_MapOverlayHdr* g_pMapOverlayHeader;

s_MapOverlayHdr* MapOverlay_Load(e_MapIdx id)
{
    const char* name = MapRegistry_GetName(id);
    if (!name || strcmp(name, "unknown") == 0)
        return NULL;

    for (int i = 0; i < REGISTRY_COUNT; i++) {
        if (strcmp(s_mapRegistry[i].name, name) == 0) {
            s_currentHeader = s_mapRegistry[i].header;
            if (!s_currentHeader)
                return NULL; /* weak symbol resolved to NULL — map not compiled */
            g_pMapOverlayHeader = s_currentHeader;
            strncpy(s_currentName, name, sizeof(s_currentName) - 1);
            return s_currentHeader;
        }
    }
    return NULL;
}

void MapOverlay_Unload(void)
{
    s_currentHeader = NULL;
    s_currentName[0] = '\0';
}

const char* MapOverlay_GetLoadedName(void)
{
    return s_currentName[0] ? s_currentName : NULL;
}
