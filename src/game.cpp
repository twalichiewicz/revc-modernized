#include "game.h"
#include "log.h"

#include <dlfcn.h>

namespace game {

// LC_UUID of the vanilla macVC 1.0.5217 binary.
const uint8_t kBuildUuid[16] = {0x02, 0x98, 0x3A, 0x69, 0x6B, 0xD0, 0x33, 0xF7,
                                0xB7, 0x2B, 0x2A, 0xCA, 0x8B, 0xCB, 0x3C, 0x33};

Api api;

namespace {

bool gOk;

template <typename T>
void Bind(T &out, const char *name)
{
	void *p = dlsym(RTLD_MAIN_ONLY, name);
	if (!p) {
		Log("game: missing symbol %s", name);
		gOk = false;
	}
	out = reinterpret_cast<T>(p);
}

} // namespace

bool Resolve()
{
	gOk = true;
	Bind(api.RwRenderStateSet, "_Z16RwRenderStateSet13RwRenderStatePv");
	Bind(api.RwIm2DRenderPrimitive, "_Z21RwIm2DRenderPrimitive15RwPrimitiveTypePN2rw3gl310Im2DVertexEi");
	Bind(api.VertSetScreenX, "_Z22RwIm2DVertexSetScreenXPN2rw3gl310Im2DVertexEf");
	Bind(api.VertSetScreenY, "_Z22RwIm2DVertexSetScreenYPN2rw3gl310Im2DVertexEf");
	Bind(api.VertSetScreenZ, "_Z22RwIm2DVertexSetScreenZPN2rw3gl310Im2DVertexEf");
	Bind(api.VertSetCameraZ, "_Z22RwIm2DVertexSetCameraZPN2rw3gl310Im2DVertexEf");
	Bind(api.VertSetRecipCameraZ, "_Z27RwIm2DVertexSetRecipCameraZPN2rw3gl310Im2DVertexEf");
	Bind(api.VertSetIntRGBA, "_Z22RwIm2DVertexSetIntRGBAPN2rw3gl310Im2DVertexEhhhh");
	Bind(api.VertSetU, "_Z16RwIm2DVertexSetUPN2rw3gl310Im2DVertexEff");
	Bind(api.VertSetV, "_Z16RwIm2DVertexSetVPN2rw3gl310Im2DVertexEff");
	Bind(api.RwTextureGetRaster, "_Z18RwTextureGetRasterPKN2rw7TextureE");
	Bind(api.GetFirstTexture, "_Z15GetFirstTexturePN2rw13TexDictionaryE");
	Bind(api.RwImageCreate, "_Z13RwImageCreateiii");
	Bind(api.RwImageAllocatePixels, "_Z21RwImageAllocatePixelsPN2rw5ImageE");
	Bind(api.RwImageGetPixels, "_Z16RwImageGetPixelsPKN2rw5ImageE");
	Bind(api.RwImageGetStride, "_Z16RwImageGetStridePKN2rw5ImageE");
	Bind(api.RwImageDestroy, "_Z14RwImageDestroyPN2rw5ImageE");
	Bind(api.RwRasterCreate, "_Z14RwRasterCreateiiii");
	Bind(api.RwRasterSetFromImage, "_Z20RwRasterSetFromImagePN2rw6RasterEPNS_5ImageE");
	Bind(api.DrawRadarSprite, "_ZN6CRadar15DrawRadarSpriteEtffh");
	Bind(api.spriteNearScreenZ, "_ZN9CSprite2d11NearScreenZE");
	Bind(api.spriteNearCamZ, "_ZN9CSprite2d8NearCamZE");
	Bind(api.spriteRecipNearClip, "_ZN9CSprite2d13RecipNearClipE");

	Bind(api.RequestModel, "_ZN10CStreaming12RequestModelEii");
	Bind(api.RemoveModel, "_ZN10CStreaming11RemoveModelEi");

	Bind(api.FindPlayerCentreOfWorld_NoSniperShift, "_Z37FindPlayerCentreOfWorld_NoSniperShiftv");
	Bind(api.PoolsGetPed, "_ZN6CPools6GetPedEi");
	Bind(api.PoolsGetVehicle, "_ZN6CPools10GetVehicleEi");
	Bind(api.PoolsGetObject, "_ZN6CPools9GetObjectEi");

	Bind(api.thePaths, "ThePaths");
	Bind(api.FindNodeClosestToCoors, "_ZN9CPathFind22FindNodeClosestToCoorsE7CVectorhfbbbb");
	Bind(api.DoPathSearch, "_ZN9CPathFind12DoPathSearchEh7CVectoriS0_PP9CPathNodePssP8CVehiclePffi");

	Bind(api.radarRange, "_ZN6CRadar12m_radarRangeE");
	Bind(api.cachedCos, "_ZN6CRadar9cachedCosE");
	Bind(api.cachedSin, "_ZN6CRadar9cachedSinE");
	Bind(api.radarOrigin, "vec2DRadarOrigin");
	Bind(api.blips, "_ZN6CRadar13ms_RadarTraceE");
	Bind(api.targetMarkerId, "_ZN6CRadar14TargetMarkerIdE");
	Bind(api.targetMarkerPos, "_ZN6CRadar15TargetMarkerPosE");
	Bind(api.radarTxdIds, "gRadarTxdIds");
	Bind(api.txdPool, "_ZN9CTxdStore11ms_pTxdPoolE");
	Bind(api.GetRadarTraceColour, "_ZN6CRadar19GetRadarTraceColourEjb");
	Bind(api.ToggleTargetMarker, "_ZN6CRadar18ToggleTargetMarkerEff");
	Bind(api.playerInStadium, "_ZN11CTheScripts21bPlayerIsInTheStatiumE");

	Bind(api.frontEndMenuManager, "FrontEndMenuManager");
	Bind(api.hudSprites, "_ZN4CHud7SpritesE");
	Bind(api.hudHideRadar, "_ZN4CHud11m_HideRadarE");
	Bind(api.timeMs, "_ZN6CTimer22m_snTimeInMillisecondsE");
	Bind(api.rsGlobal, "RsGlobal");
	Bind(api.GetPad, "_ZN4CPad6GetPadEi");

	Bind(api.FontPrintString, "_ZN5CFont11PrintStringEffPt");
	Bind(api.FontSetScale, "_ZN5CFont8SetScaleEff");
	Bind(api.FontSetColor, "_ZN5CFont8SetColorE5CRGBA");
	Bind(api.FontSetFontStyle, "_ZN5CFont12SetFontStyleEs");
	Bind(api.FontSetPropOff, "_ZN5CFont10SetPropOffEv");
	Bind(api.FontSetJustifyOff, "_ZN5CFont13SetJustifyOffEv");
	Bind(api.FontSetCentreOff, "_ZN5CFont12SetCentreOffEv");
	Bind(api.FontSetRightJustifyOn, "_ZN5CFont17SetRightJustifyOnEv");
	Bind(api.FontSetRightJustifyWrap, "_ZN5CFont19SetRightJustifyWrapEf");
	Bind(api.FontSetBackgroundOn, "_ZN5CFont15SetBackgroundOnEv");
	Bind(api.FontSetBackgroundOff, "_ZN5CFont16SetBackgroundOffEv");
	Bind(api.FontSetBackgroundColor, "_ZN5CFont18SetBackgroundColorE5CRGBA");
	Bind(api.FontSetBackGroundOnlyTextOff, "_ZN5CFont24SetBackGroundOnlyTextOffEv");
	Bind(api.FontSetDropShadowPosition, "_ZN5CFont21SetDropShadowPositionEs");
	Bind(api.FontGetStringWidth, "_ZN5CFont14GetStringWidthEPtb");
	Bind(api.FontGetTextRect, "_ZN5CFont11GetTextRectEP5CRectffPt");
	Bind(api.AsciiToUnicode, "_Z14AsciiToUnicodePKcPt");
	Bind(api.clockHours, "_ZN6CClock18ms_nGameClockHoursE");
	Bind(api.clockMinutes, "_ZN6CClock20ms_nGameClockMinutesE");

	Bind(api.FindPlayerPed, "_Z13FindPlayerPedv");
	Bind(api.worldPlayers, "_ZN6CWorld7PlayersE");
	Bind(api.playerInFocus, "_ZN6CWorld13PlayerInFocusE");
	return gOk;
}

// Mirrors CRadar::DrawRadarSection: CTxdStore::GetSlot(gRadarTxdIds[x + 8y])->texDict.
RwTexture *RadarTileTexture(int x, int y)
{
	if (x < 0 || y < 0 || x >= kRadarTiles || y >= kRadarTiles || *api.playerInStadium)
		return nullptr;
	const int32_t slot = api.radarTxdIds[x + kRadarTiles * y];
	const uint8_t *pool = *api.txdPool;
	if (slot < 0 || !pool || slot >= *reinterpret_cast<const int32_t *>(pool + 0x10))
		return nullptr;
	const uint8_t *entries = *reinterpret_cast<uint8_t *const *>(pool);
	auto *dict = *reinterpret_cast<RwTexDictionary *const *>(entries + 32 * slot);
	return dict ? api.GetFirstTexture(dict) : nullptr;
}

} // namespace game
