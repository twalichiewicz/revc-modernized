#pragma once
// The slice of the game this plugin talks to, resolved by symbol name from the
// main executable. Struct offsets were read from the disassembly of macVC
// 1.0.5217 (LC_UUID below); hooks are refused on any other build.
#include "types.h"

#include <cstddef>
#include <cstdint>

struct CVector2D {
	float x, y;
};
struct CVector {
	float x, y, z;
};
struct CRect;
struct CSprite2d;
struct CPathNode;
struct CVehicle;
struct RwIm2DVertex; // rw::gl3::Im2DVertex, 28 bytes
struct RwTexture;
struct RwRaster;
struct RwImage;
struct RwTexDictionary;

namespace game {

extern const uint8_t kBuildUuid[16];

// RenderWare render states / values (RwRenderStateSet takes RW enums).
enum : int {
	kStateTextureRaster = 1,
	kStateTextureAddress = 2,
	kStateZTest = 6,
	kStateZWrite = 8,
	kStateTextureFilter = 9,
	kStateSrcBlend = 10,
	kStateDestBlend = 11,
	kStateVertexAlpha = 12,
	kStateFog = 14,
	kBlendSrcAlpha = 5,
	kBlendInvSrcAlpha = 6,
	kFilterLinear = 2,
	kAddressClamp = 3,
	kPrimTriList = 3,
	kRasterTypeTexture = 0x04,
	kRasterFormat8888 = 0x0500,
	kRadarSpriteNorth = 3,
};

constexpr int kIm2DVertexSize = 28;
constexpr int kNumBlips = 75;
constexpr int kStreamTxdOffset = 6500; // CStreaming index of TXD slot 0
constexpr int kRadarTiles = 8;         // the radar map is 8x8 tiles of 500 units
constexpr float kRadarTileSize = 500.0f;
constexpr float kWorldMin = -2000.0f;

// tRadarTrace (stride 0x38)
struct Blip {
	uint32_t colour;
	uint32_t type; // eBlipType
	int32_t entityHandle;
	CVector pos2D; // radar position
	CVector pos;   // 3D marker position
	uint16_t uniqueIndex;
	uint8_t dim, inUse, shortRange;
	uint8_t pad[3];
	float radius;
	int16_t scale;
	uint16_t display; // eBlipDisplay: 2 = blip only, 3 = blip + marker
	uint16_t sprite;  // 0 = plain coloured blip
	uint16_t pad2;
};
static_assert(sizeof(Blip) == 0x38, "tRadarTrace layout");
static_assert(__builtin_offsetof(Blip, pos) == 0x18 && __builtin_offsetof(Blip, shortRange) == 0x28 &&
                  __builtin_offsetof(Blip, display) == 0x32 && __builtin_offsetof(Blip, sprite) == 0x34,
              "tRadarTrace field offsets (see CRadar::DrawCoordBlip)");

enum BlipType : uint32_t { kBlipCar = 1, kBlipChar = 2, kBlipObject = 3, kBlipCoord = 4, kBlipContact = 5 };

struct Api {
	// RenderWare / 2D drawing
	void (*RwRenderStateSet)(int state, void *value);
	void (*RwIm2DRenderPrimitive)(int prim, RwIm2DVertex *verts, int count);
	void (*VertSetScreenX)(RwIm2DVertex *, float);
	void (*VertSetScreenY)(RwIm2DVertex *, float);
	void (*VertSetScreenZ)(RwIm2DVertex *, float);
	void (*VertSetCameraZ)(RwIm2DVertex *, float);
	void (*VertSetRecipCameraZ)(RwIm2DVertex *, float);
	void (*VertSetIntRGBA)(RwIm2DVertex *, uint8_t, uint8_t, uint8_t, uint8_t);
	void (*VertSetU)(RwIm2DVertex *, float u, float recipZ);
	void (*VertSetV)(RwIm2DVertex *, float v, float recipZ);
	RwRaster *(*RwTextureGetRaster)(const RwTexture *);
	RwTexture *(*GetFirstTexture)(RwTexDictionary *);
	RwImage *(*RwImageCreate)(int width, int height, int depth);
	RwImage *(*RwImageAllocatePixels)(RwImage *);
	uint8_t *(*RwImageGetPixels)(const RwImage *);
	int (*RwImageGetStride)(const RwImage *);
	void (*RwImageDestroy)(RwImage *);
	RwRaster *(*RwRasterCreate)(int width, int height, int depth, int flags);
	RwRaster *(*RwRasterSetFromImage)(RwRaster *, RwImage *);
	void (*DrawRadarSprite)(uint16_t sprite, float x, float y, uint8_t alpha);
	float *spriteNearScreenZ, *spriteNearCamZ, *spriteRecipNearClip;

	// streaming
	void (*RequestModel)(int index, int flags);
	void (*RemoveModel)(int index);

	// player / world
	CVector *(*FindPlayerCentreOfWorld_NoSniperShift)();
	uint8_t *(*PoolsGetPed)(int handle);
	uint8_t *(*PoolsGetVehicle)(int handle);
	uint8_t *(*PoolsGetObject)(int handle);

	// paths
	uint8_t *thePaths;
	int (*FindNodeClosestToCoors)(void *self, CVector pos, uint8_t type, float distLimit, bool ignoreDisabled,
	                              bool ignoreBetweenLevels, bool ignoreFlagB4, bool waterPath);
	void (*DoPathSearch)(void *self, uint8_t type, CVector start, int startNode, CVector target, CPathNode **nodes,
	                     int16_t *numNodes, int16_t maxNodes, CVehicle *vehicle, float *dist, float distLimit,
	                     int forcedTargetNode);

	// radar state
	float *radarRange;
	float *cachedCos, *cachedSin;
	CVector2D *radarOrigin;
	Blip *blips;
	int32_t *targetMarkerId;
	CVector2D *targetMarkerPos;
	int32_t *radarTxdIds; // [64]
	uint8_t **txdPool;    // CPool<TxdDef>*: entries at +0 (32-byte TxdDef, texDict first), size at +0x10
	uint32_t (*GetRadarTraceColour)(uint32_t colour, bool dim);
	void (*ToggleTargetMarker)(float x, float y);
	uint8_t *playerInStadium;

	// UI state
	uint8_t *frontEndMenuManager;
	uint8_t *hudSprites;
	uint8_t *hudHideRadar;
	uint32_t *timeMs;
	uint8_t *rsGlobal; // RsGlobal: screen width/height at +8/+0xc
	uint8_t *(*GetPad)(int index);

	// text (CFont state applies to the next PrintString; text is queued for DrawFonts)
	void (*FontPrintString)(float x, float y, uint16_t *text);
	void (*FontSetScale)(float x, float y);
	void (*FontSetColor)(CRGBA);
	void (*FontSetFontStyle)(int16_t);
	void (*FontSetPropOff)();
	void (*FontSetJustifyOff)();
	void (*FontSetCentreOff)();
	void (*FontSetRightJustifyOn)();
	void (*FontSetRightJustifyWrap)(float);
	void (*FontSetBackgroundOn)();
	void (*FontSetBackgroundOff)();
	void (*FontSetBackgroundColor)(CRGBA);
	void (*FontSetBackGroundOnlyTextOff)();
	void (*FontSetDropShadowPosition)(int16_t);
	float (*FontGetStringWidth)(uint16_t *text, bool spaces);
	void (*FontGetTextRect)(float *rect, float x, float y, uint16_t *text); // CRect*
	void (*AsciiToUnicode)(const char *, uint16_t *);
	void (*FontSetPropOn)();
	void (*FontSetWrapx)(float);
	uint8_t *fontDetails; // CFont::Details: wrap x at +0x2c
	void (*FontDrawFonts)();
	uint8_t *clockHours, *clockMinutes;

	// frontend
	uint8_t *menuScreens; // aScreens (CMenuScreen[], see MenuPageEntry)
	float *menuFrame;     // menuBg: the pause screens' slanted frame, 4 corners in 640x448 units
	void (*SwitchToNewScreen)(uint8_t *menu, int8_t page); // 0: the current entry's target, -2: back
	void (*RequestFrontEndShutDown)(uint8_t *menu);
	int (*GetOptionCount)(int page);
	uint8_t *dmAudio;
	void (*PlayFrontEndSound)(uint8_t *audio, uint16_t sound, uint32_t volume);
	uint8_t *newKeyState, *oldKeyState;     // CKeyboardState: int16 F[12], then VK_KEYS[256]
	uint8_t *newMouseState, *oldMouseState; // CMouseControllerState: LMB at +0

	// player
	uint8_t *(*FindPlayerPed)();
	uint8_t *worldPlayers; // CWorld::Players, CPlayerInfo[] with stride 0x1a0
	uint8_t *playerInFocus;
	uint8_t *pickups;      // CPickups::aPickUps, CPickup[kNumPickups]
};

extern Api api;

bool Resolve();

inline bool MenuMapActive() { return api.frontEndMenuManager[0x35] != 0; }

// --- frontend (CMenuManager, aScreens) ---------------------------------------
enum MenuPage : int32_t {
	kPageStats = 0,
	kPageStartGame = 1, // new / load / delete game
	kPageBriefs = 2,
	kPageMap = 6,
	kPageOptions = 27,
	kPagePause = 32,
	kPageNone = 33, // as a previous page: the pause menu in game, else the main menu
};
constexpr size_t kMenuPageSize = 752; // CMenuScreen
constexpr int kMenuEntries = 18;
constexpr size_t kMenuEntrySize = 40; // CMenuEntry: action at +0, name[8] at +4, target page at +28
inline uint8_t *MenuPageEntry(int32_t page, int entry)
{
	return api.menuScreens + size_t(page) * kMenuPageSize + 32 + size_t(entry) * kMenuEntrySize;
}
inline int32_t MenuPreviousPage(int32_t page)
{
	return *reinterpret_cast<const int32_t *>(api.menuScreens + size_t(page) * kMenuPageSize + 8);
}
inline int32_t &MenuScreen(uint8_t *menu) { return *reinterpret_cast<int32_t *>(menu + 0x16c); }
inline int32_t MenuScreen(const uint8_t *menu) { return *reinterpret_cast<const int32_t *>(menu + 0x16c); }
inline int32_t &MenuOption(uint8_t *menu) { return *reinterpret_cast<int32_t *>(menu + 0x30); }
inline int32_t &MenuOptionCount(uint8_t *menu) { return *reinterpret_cast<int32_t *>(menu + 0x6f8); }
inline bool MenuActive(const uint8_t *menu) { return menu[0x38] != 0; }
inline bool MenuGameNotLoaded(const uint8_t *menu) { return menu[0x6c] != 0; }
inline int MenuMouseX(const uint8_t *menu) { return *reinterpret_cast<const int32_t *>(menu + 0x1a0); }
inline int MenuMouseY(const uint8_t *menu) { return *reinterpret_cast<const int32_t *>(menu + 0x1a4); }
constexpr uint16_t kSoundMenuHighlight = 0xc3;

// --- input -------------------------------------------------------------------
inline bool KeyDown(const uint8_t *state, int c) { return *reinterpret_cast<const int16_t *>(state + 24 + 2 * c) != 0; }
// VK_KEYS is indexed by character, and the game checks both cases.
inline bool CharJustDown(char upper)
{
	const char lower = char(upper - 'A' + 'a');
	return (KeyDown(api.newKeyState, upper) && !KeyDown(api.oldKeyState, upper)) ||
	       (KeyDown(api.newKeyState, lower) && !KeyDown(api.oldKeyState, lower));
}
inline bool LeftClickJustDown() { return api.newMouseState[0] && !api.oldMouseState[0]; }
// CPad: NewState then OldState (CControllerState, 0x2a bytes of int16 buttons).
enum PadButton : int {
	kPadLB = 0x8,
	kPadLT = 0xa,
	kPadRB = 0xc,
	kPadTriangle = 0x1e,
	kPadCross = 0x20,
	kPadCircle = 0x22,
};
constexpr int kPadOldState = 0x2a;
inline int16_t &PadNew(uint8_t *pad, PadButton b) { return *reinterpret_cast<int16_t *>(pad + b); }
inline int16_t &PadOld(uint8_t *pad, PadButton b) { return *reinterpret_cast<int16_t *>(pad + kPadOldState + b); }
inline bool CrossJustDown(const uint8_t *pad)
{
	auto at = [pad](int off) { return *reinterpret_cast<const int16_t *>(pad + off); };
	return at(kPadCross) && !at(kPadOldState + kPadCross);
}
inline int ScreenWidth() { return *reinterpret_cast<const int32_t *>(api.rsGlobal + 8); }
inline int ScreenHeight() { return *reinterpret_cast<const int32_t *>(api.rsGlobal + 0xc); }
// CPed / CPlayerPed / CPlayerInfo fields (see CHud::Draw, CPlayerPed::UseSprintEnergy,
// CPlayerInfo::SavePlayerInfo).
inline float PedField(const uint8_t *ped, int off) { return *reinterpret_cast<const float *>(ped + off); }
inline float Health(const uint8_t *ped) { return PedField(ped, 0x44c); }
inline float Armour(const uint8_t *ped) { return PedField(ped, 0x450); }
inline float Stamina(const uint8_t *player) { return PedField(player, 0x78c); }    // sprinting needs > 0
inline float MaxStamina(const uint8_t *player) { return PedField(player, 0x790); }
inline const uint8_t *PlayerInfo() { return api.worldPlayers + size_t(*api.playerInFocus) * 0x1a0; }
inline bool InfiniteSprint(const uint8_t *info) { return info[0x16c] != 0; }
inline int MaxHealth(const uint8_t *info) { return info[0x16f]; }
inline int MaxArmour(const uint8_t *info) { return info[0x170]; }
// The HUD's money, counting towards the real amount at +0xc8.
inline int32_t VisibleMoney(const uint8_t *info) { return *reinterpret_cast<const int32_t *>(info + 0xcc); }
// CPickup (64 bytes, see CPickups::GenerateNewOne): position at +0, type at +0x36, removed at +0x37.
constexpr int kNumPickups = 336;
enum PickupType : uint8_t { kPickupInShop = 1, kPickupInShopOutOfStock = 7, kPickupPropertyForSale = 18 };
inline const uint8_t *Pickup(int i) { return api.pickups + size_t(i) * 64; }
inline const CVector &PickupPos(const uint8_t *p) { return *reinterpret_cast<const CVector *>(p); }
inline int32_t RadarMode() { return *reinterpret_cast<int32_t *>(api.frontEndMenuManager + 0xc); }
inline const CSprite2d *RadarDiscSprite() { return reinterpret_cast<const CSprite2d *>(api.hudSprites + 0x190); }
inline const CVector &EntityPos(const uint8_t *entity) { return *reinterpret_cast<const CVector *>(entity + 0x38); }
inline bool PedInVehicle(const uint8_t *ped) { return ped[0x4c0] != 0; }
inline uint8_t *PedVehicle(const uint8_t *ped) { return *reinterpret_cast<uint8_t *const *>(ped + 0x4b8); }
inline int32_t NumCarPathNodes() { return *reinterpret_cast<int32_t *>(api.thePaths + 0x54b7c); }
inline CVector NodePos(const CPathNode *node)
{
	auto *p = reinterpret_cast<const int16_t *>(reinterpret_cast<const uint8_t *>(node) + 4);
	return {p[0] / 8.0f, p[1] / 8.0f, p[2] / 8.0f};
}
RwTexture *RadarTileTexture(int x, int y);

} // namespace game
