#include "common.h"
#include "game.h"
#include "bodyprog/map/map.h"

extern s_MapOverlayHdr g_MapOverlayHeader_map0_s00;
extern s_MapOverlayHdr* g_pMapOverlayHeader;

s_MapOverlayHdr* MapOverlay_Load(e_MapIdx id)
{
    (void)id;
    g_pMapOverlayHeader = &g_MapOverlayHeader_map0_s00;
    return g_pMapOverlayHeader;
}

void MapOverlay_Unload(void)
{
    /* no DLLs on Switch */
}

const char* MapOverlay_GetLoadedName(void)
{
    return "map0_s00";
}
