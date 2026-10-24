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
void Bind(T &out, const char *name, void *handle = RTLD_MAIN_ONLY, bool required = true)
{
	void *p = dlsym(handle, name);
	if (!p) {
		Log("game: missing symbol %s", name);
		gOk = gOk && !required;
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
	Bind(api.FontSetPropOn, "_ZN5CFont9SetPropOnEv");
	Bind(api.FontSetWrapx, "_ZN5CFont8SetWrapxEf");
	Bind(api.fontDetails, "_ZN5CFont7DetailsE");
	Bind(api.FontDrawFonts, "_ZN5CFont9DrawFontsEv");
	Bind(api.menuScreens, "aScreens");
	Bind(api.menuFrame, "menuBg");
	Bind(api.SwitchToNewScreen, "_ZN12CMenuManager17SwitchToNewScreenEa");
	Bind(api.RequestFrontEndShutDown, "_ZN12CMenuManager23RequestFrontEndShutDownEv");
	Bind(api.GetOptionCount, "_Z14GetOptionCounti");
	Bind(api.dmAudio, "DMAudio");
	Bind(api.PlayFrontEndSound, "_ZN8cDMAudio17PlayFrontEndSoundEtj");
	Bind(api.newKeyState, "_ZN4CPad11NewKeyStateE");
	Bind(api.oldKeyState, "_ZN4CPad11OldKeyStateE");
	Bind(api.newMouseState, "_ZN4CPad23NewMouseControllerStateE");
	Bind(api.oldMouseState, "_ZN4CPad23OldMouseControllerStateE");
	Bind(api.padAffectedByController, "_ZN4CPad22IsAffectedByControllerE");
	Bind(api.invertLookForPad, "_ZN4CPad15bInvertLook4PadE");
	Bind(api.clockHours, "_ZN6CClock18ms_nGameClockHoursE");
	Bind(api.clockMinutes, "_ZN6CClock20ms_nGameClockMinutesE");

	Bind(api.FindPlayerPed, "_Z13FindPlayerPedv");
	Bind(api.worldPlayers, "_ZN6CWorld7PlayersE");
	Bind(api.playerInFocus, "_ZN6CWorld13PlayerInFocusE");
	Bind(api.pickups, "_ZN8CPickups8aPickUpsE");

	Bind(api.GetWeaponInfo, "_ZN11CWeaponInfo13GetWeaponInfoE11eWeaponType");
	Bind(api.WeaponHasAmmoToBeUsed, "_ZN7CWeapon21HasWeaponAmmoToBeUsedEv");
	Bind(api.SetCurrentWeapon, "_ZN4CPed16SetCurrentWeaponEi");
	Bind(api.modelInfoPtrs, "_ZN10CModelInfo16ms_modelInfoPtrsE");
	Bind(api.TexDictionaryFindNamedTexture, "_Z31RwTexDictionaryFindNamedTexturePN2rw13TexDictionaryEPKc");
	Bind(api.TxdFindSlot, "_ZN9CTxdStore11FindTxdSlotEPKc");
	Bind(api.TxdAddSlot, "_ZN9CTxdStore10AddTxdSlotEPKc");
	Bind(api.TxdLoad, "_ZN9CTxdStore7LoadTxdEiPKc");
	Bind(api.TxdAddRef, "_ZN9CTxdStore6AddRefEi");
	Bind(api.timeScale, "_ZN6CTimer13ms_fTimeScaleE");
	Bind(api.theCamera, "TheCamera");
	Bind(api.mouseAccelH, "_ZN7CCamera20m_fMouseAccelHorzntlE");
	Bind(api.mouseAccelV, "_ZN7CCamera21m_fMouseAccelVerticalE");
	Bind(api.useMouse3rdPerson, "_ZN7CCamera20m_bUseMouse3rdPersonE");
	Bind(api.crosshairMultX, "_ZN7CCamera22m_f3rdPersonCHairMultXE");
	Bind(api.crosshairMultY, "_ZN7CCamera22m_f3rdPersonCHairMultYE");
	Bind(api.GetIsLineOfSightClear, "_ZN6CWorld21GetIsLineOfSightClearERK7CVectorS2_bbbbbbb");
	Bind(api.Find3rdPersonCamTargetVector, "_ZN7CCamera28Find3rdPersonCamTargetVectorEf7CVectorRS0_S1_");
	Bind(api.ProcessLineOfSight, "_ZN6CWorld18ProcessLineOfSightERK7CVectorS2_R9CColPointRP7CEntitybbbbbbbb");
	Bind(api.ignoreEntity, "_ZN6CWorld13pIgnoreEntityE");
	Bind(api.GetAnimHierarchyFromSkinClump, "_Z29GetAnimHierarchyFromSkinClumpPN2rw5ClumpE");
	Bind(api.HAnimIDGetIndex, "_Z17RpHAnimIDGetIndexPN2rw14HAnimHierarchyEi");
	Bind(api.HAnimMatrixArray, "_Z30RpHAnimHierarchyGetMatrixArrayPN2rw14HAnimHierarchyE");
	Bind(api.UpdateRpHAnim, "_ZN7CEntity13UpdateRpHAnimEv");
	Bind(api.RtQuatRotate, "_Z12RtQuatRotateP6RtQuatPKN2rw3V3dEf15RwOpCombineType");
	Bind(api.BlendAnimation, "_ZN12CAnimManager14BlendAnimationEPN2rw5ClumpE12AssocGroupId11AnimationIdf");
	Bind(api.GetAssociation, "_Z30RpAnimBlendClumpGetAssociationPN2rw5ClumpEj");
	Bind(api.SetFinishCallback, "_ZN21CAnimBlendAssociation17SetFinishCallbackEPFvPS_PvES1_");
	Bind(api.AddWeaponModel, "_ZN4CPed14AddWeaponModelEi");
	Bind(api.RemoveWeaponModel, "_ZN4CPed17RemoveWeaponModelEi");
	Bind(api.IsPedInControl, "_ZN4CPed14IsPedInControlEv");
	Bind(api.PadGetTarget, "_ZN4CPad9GetTargetEv");
	Bind(api.PadGetWeapon, "_ZN4CPad9GetWeaponEv");
	Bind(api.scriptSpace, "_ZN11CTheScripts11ScriptSpaceE");
	Bind(api.activeScripts, "_ZN11CTheScripts14pActiveScriptsE");

	Bind(api.numRetunePresses, "gNumRetunePresses");
	Bind(api.retuneCounter, "gRetuneCounter");
	Bind(api.radioStaticCounter, "RadioStaticCounter");
	Bind(api.metDebbieHarry, "_ZN11CTheScripts24bPlayerHasMetDebbieHarryE");
	Bind(api.VehicleUsesSiren, "_ZN8CVehicle9UsesSirenEv");
	Bind(api.IsMP3RadioChannelAvailable, "_ZN8cDMAudio26IsMP3RadioChannelAvailableEv");
	Bind(api.TextGet, "_ZN5CText3GetEPKc");
	Bind(api.theText, "TheText");

	Bind(api.glfwJoystickIsGamepad, "glfwJoystickIsGamepad", RTLD_DEFAULT, false); // only for controls.cpp
	Bind(api.glfwGetGamepadState, "glfwGetGamepadState", RTLD_DEFAULT, false);
	Bind(api.glfwGetJoystickName, "glfwGetJoystickName", RTLD_DEFAULT, false);
	Bind(api.glfwUpdateGamepadMappings, "glfwUpdateGamepadMappings", RTLD_DEFAULT, false);
	return gOk;
}

// CTxdStore::GetSlot(slot)->texDict
RwTexDictionary *TxdDictionary(int slot)
{
	const uint8_t *pool = *api.txdPool;
	if (slot < 0 || !pool || slot >= *reinterpret_cast<const int32_t *>(pool + 0x10))
		return nullptr;
	const uint8_t *entries = *reinterpret_cast<uint8_t *const *>(pool);
	return *reinterpret_cast<RwTexDictionary *const *>(entries + 32 * slot);
}

// Mirrors CRadar::DrawRadarSection.
RwTexture *RadarTileTexture(int x, int y)
{
	if (x < 0 || y < 0 || x >= kRadarTiles || y >= kRadarTiles || *api.playerInStadium)
		return nullptr;
	RwTexDictionary *dict = TxdDictionary(api.radarTxdIds[x + kRadarTiles * y]);
	return dict ? api.GetFirstTexture(dict) : nullptr;
}

RwTexture *WeaponIcon(int32_t type)
{
	const uint8_t *info = api.GetWeaponInfo(type);
	const int32_t model = info ? *reinterpret_cast<const int32_t *>(info + 0x54) : -1;
	const uint8_t *mi = model > 0 ? api.modelInfoPtrs[model] : nullptr;
	RwTexDictionary *dict = mi ? TxdDictionary(*reinterpret_cast<const int16_t *>(mi + 0x2e)) : nullptr;
	return dict ? api.TexDictionaryFindNamedTexture(dict, reinterpret_cast<const char *>(mi + 8)) : nullptr;
}

bool RadioUsable(uint8_t *vehicle)
{
	switch (EntityModel(vehicle)) {
	case 162: case 163: case 202: case 218: case 227: // Rhino, Barracks, Coast Guard, the Mavericks
		return false;
	case 153: case 155: // Mr Whoopee and the Hunter keep their radio despite the "siren"
		return true;
	case 216: // Kaufman cab
		return !*api.metDebbieHarry;
	default:
		return !api.VehicleUsesSiren(vehicle);
	}
}

int RadioTuning(const uint8_t *vehicle)
{
	return ((VehicleRadio(vehicle) + *api.numRetunePresses) % kRadioChannels + kRadioChannels) % kRadioChannels;
}

void RetuneBy(const uint8_t *vehicle, int steps, int frames)
{
	if (steps == 0)
		return;
	// The MP3 player is only in the cycle when it has tracks.
	const int dir = steps > 0 ? 1 : -1;
	int target = ((RadioTuning(vehicle) + steps) % kRadioChannels + kRadioChannels) % kRadioChannels;
	if (target == kRadioMP3 && !api.IsMP3RadioChannelAvailable(api.dmAudio))
		steps += dir;
	*api.numRetunePresses += steps;
	*api.retuneCounter = frames;
	*api.radioStaticCounter = 0;
}

} // namespace game
