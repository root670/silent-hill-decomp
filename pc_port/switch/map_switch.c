#include "common.h"
#include "game.h"
#include "bodyprog/map/map.h"

extern s_MapOverlayHdr g_MapOverlayHeader_map0_s00;
extern s_MapOverlayHdr* g_pMapOverlayHeader;

/* On Switch only map0_s00 is compiled in; all other map IDs fall back to it. */
s_MapOverlayHdr* MapOverlay_Load(e_MapIdx id)
{
    (void)id;
    g_pMapOverlayHeader = &g_MapOverlayHeader_map0_s00;
    return g_pMapOverlayHeader;
}

void MapOverlay_Unload(void)
{
    /* nothing to unload — no DLLs on Switch */
}

const char* MapOverlay_GetLoadedName(void)
{
    return "map0_s00";
}
