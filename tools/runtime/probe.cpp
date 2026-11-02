// Standalone diagnostic sidecar. Deliberately excluded from the plugin Makefile.
// Native layouts are the verified macVC 1.0.5217 layouts in src/game.h.
#include "probe_protocol.h"
#include "probe_joints.h"
#include "probe_actions.h"
#include <cstddef>
#include "probe_offsets.h" // generated from this build's retained plugin symbols
#include "../../src/game.h"
#include "../../src/body.h"
#include "../../src/reactphysics.h"
#include "../../src/aircraft.h"
#include "../../src/helizones.h"
#include "../../src/heat.h"
#include "../../src/sight.h"
#include "../../src/config.h"
#include "../../src/jetwheels.h"

#include <CoreFoundation/CoreFoundation.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <crt_externs.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <mach/mach_time.h>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <mach-o/loader.h>
#include <pthread.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
constexpr uint8_t kGameUuid[16] = {0x02,0x98,0x3a,0x69,0x6b,0xd0,0x33,0xf7,0xb7,0x2b,0x2a,0xca,0x8b,0xcb,0x3c,0x33};
constexpr size_t kMaxLog = 1024 * 1024, kMaxSamples = 36000;
struct Api {
    uint8_t *(*player)();
    uint8_t *(*ped)(int), *(*vehicle)(int);
    int (*pedRef)(uint8_t *), (*vehicleRef)(uint8_t *);
    void (*teleport)(uint8_t *, CVector);
    void (*collision)(const CVector &), (*scene)(const CVector &);
    void (*clock)(uint8_t, uint8_t);
    void (*weather)(int16_t), (*weatherRelease)();
    void (*cameraPosition)(uint8_t *, const CVector &, const CVector &);
    void (*cameraTarget)(uint8_t *, const CVector &, int16_t, int);
    void (*cameraRestore)(uint8_t *);
    void (*componentPosition)(void *,CVector *,uint32_t);
    void (*request)(int, int);
    uint8_t *(*addPed)(int, unsigned, const CVector &, int);
    void (*removePed)(uint8_t *);
    float (*ground)(float, float, float, bool *);
    bool (*vertical)(const CVector &,float,uint8_t *,uint8_t *&,bool,bool,bool,bool,bool,bool,void *);
    bool (*line)(const CVector &,const CVector &,uint8_t *,uint8_t *&,bool,bool,bool,bool,bool,bool,bool,bool);
    bool (*damage)(uint8_t *, uint8_t *, int, float, int, uint8_t);
    void (*wanted)(uint8_t *, int);
    void (*objectiveAt)(uint8_t *, int32_t, CVector);
    void (*bulletImpact)(uint8_t *,uint8_t *,uint8_t *,CVector *,CVector *,uint8_t *,CVector2D);
    uint8_t *(*hierarchy)(uint8_t *);
    uint8_t *(*association)(uint8_t *, uint32_t);
    float *(*matrices)(uint8_t *);
    int (*boneIndex)(uint8_t *,int);
    uint8_t *(*weaponInfo)(int);
    void (*setWeaponSlot)(uint8_t *,int);
    uint8_t *(*pad)(int),*padAffected;bool (*onMission)();
    void (*updateMatrix)(void *),(*updateFrame)(uint8_t *);
    void *(*cocoaWindow)(void *);
    int (*windowShouldClose)(void *);
    int (*joystickPresent)(int),(*joystickIsGamepad)(int),(*gamepadState)(int,void *);
    const char *(*joystickName)(int);
    const float *(*joystickAxes)(int,int *);
    uint8_t *menu, *camera, *stream, *rsGlobal;
    uint8_t *particleSystems,*particleArray;int32_t *particleInterval;
    uint8_t **pedPool, **vehiclePool, **helis;
    uint32_t *time, *frame, *clockTick;
    int32_t *area;
    uint8_t *hour, *minute, *freeze, *slow, *userPause, *codePause;
    uint16_t *seconds;
    bool *cutscene;
    int16_t *weatherOld, *weatherNew, *weatherForced, *heliCount;
    float *weatherInterpolation, *rain, *clouds, *timeScale;
    bool (*rtEnabled)(), (*rtActive)();
    int (*foreground)();
    float *sunX, *sunY, *ambient[3], *directional[3];
    int32_t *shadowStrength;
    int32_t *sunIndex;
    CVector *sunVectors;
    uint8_t **models;
    uint8_t *lights;
    int16_t *lightCount;
    uint8_t *pipeEnable[3];float *pipeMult[3];
    int32_t *filterMode;float *filterIntensity;
    float *blur[3], *extraColourInterpolation;
    int32_t *extraColourOn, *extraColour;
} g;
struct Header { char magic[8]; uint32_t version, count; uint8_t uuid[16]; };
struct Entry { uint32_t hash, func, slot, original, kind; };
static_assert(sizeof(Header) == 32 && sizeof(Entry) == 20, "patch table v2 ABI");
using Hud = void (*)();
Hud gNextHud;
void **gHudSlot;
const uint8_t *gMain;
uint8_t *gGrade;
uint8_t *gClouds;
const uint8_t *gPlanesEnabled,*gPlaneForcesReady,*gPlaneRosterReady;
const uint32_t *gPlaneAttempt,*gPlanePrevious;
const uint8_t *gAirportReady,*gAirportShapeReady;
const int32_t *gAirportModel;
using WheelSnapshot=bool (*)(const uint8_t *,std::array<jetwheels::WheelPose,3> &);
WheelSnapshot gJetWheelSnapshot;
float (*gJetGearDown)(const uint8_t *);
float (*gJetEnginePower)(const uint8_t *);
const uint8_t *gFirstPerson,*gCameraHeadReady,*gCameraFirstPerson;
const uint8_t *const *gCameraCockpit,*const *gCameraHeadAbout;
const float *gCameraLookPitch,*gCameraLookYaw,*gCameraHeadX,*gCameraHeadY,*gCameraHeadZ;
const uint8_t *gPeople;
const uint8_t *gWanted, *gEyes, *gWear, *gCrews;
using BodyPoint = Vec3 (*)(const reactphysics::Body *);
BodyPoint gPhysicalNeck, gPhysicalHeadAxis;
probe::WeaponSnapshot gWeaponSnapshot;
probe::WindowTransition gWindowTransition;
SEL gStyleSelector, gToggleSelector;
std::string gCommand, gStatus, gLog, gFrames;
bool gSymbols, gInHud, gReady, gWasReady, gCapture, gCameraOwned, gOriginals;
bool gGradeInitiallyReady;
bool gToggleOriginals;
bool gHiddenMatched;
uint8_t gOriginalFreeze, gOriginalRt, gOriginalGrade;
uint8_t gOriginalClouds,gOriginalPipeEnable[3];float gOriginalPipeMult[3];
int32_t gOriginalFilterMode;float gOriginalFilterIntensity;
uint8_t gOriginalHour, gOriginalMinute;
int16_t gOriginalWeatherOld, gOriginalWeatherNew, gOriginalWeatherForced;
uint32_t gPreviousTime;
uint64_t gLastId, gLastPollNs, gLastStatusNs, gLastFrameNs, gHudCalls;
int gOwned = -1;
uint8_t *gOwnedPointer;
std::string gLastCommand = "none", gResult = "waiting for plugin HUD hook", gLabel;
mach_timebase_info_data_t gTimebase;

// Read only. Mirrors reactions.cpp's Person solely to validate the vector stride.
// No clump, joint, or skeleton pointer in this mirror is followed.
struct ReactionPerson {
    int handle;
    uint8_t *clump;
    std::array<int, body::kBones> indices;
    std::array<int, reactphysics::Count> joints;
    std::array<std::array<float,16>, body::kBones> original;
    reactphysics::Body physical;
    Vec3 pelvisOffset, publishedVelocity;
    uint32_t stepped, hitAt, publishedAt, savedGetUpTimer;
    reactphysics::Mode nativeMode;
    bool saved, heldFall, pendingVehicle;
};
static_assert(sizeof(reactphysics::Body)==736,"rebuild/reverify the diagnostic Body mirror after solver layout changes");
struct WantedModel {
    heat::State state; int stars, escaped; bool calling;
    heat::Description description;
    Vec3 lastSeen, searchCentre;
    uint32_t lastSeenAt, searchSince;
};
struct Crew {
    int heli, ped[2]; bool down[2], logged; int pilot; bool pilotShot;
};
struct Wear {
    int handle,lampHits,rotorHits;bool lampOut,rotorOut;
    helizones::Lamp lamp;CVector spot;float light;aircraft::Crash crash;
    CVector escape;uint32_t escapeUntil;void *tailRotor;bool rotorLookedUp;
    uint32_t arrivalAt,setupSamples;double setupMaxMs,setupTotalMs;bool setupLogged;
};
static_assert(sizeof(WantedModel)==96 && sizeof(Crew)==24 && sizeof(Wear)==152,
              "mirrored read-only plugin layouts changed: reverify before enabling");
static_assert(offsetof(WantedModel,description)==16 && offsetof(WantedModel,lastSeen)==64 &&
              offsetof(Crew,pilot)==16 && offsetof(Wear,lamp)==16 && offsetof(Wear,light)==52 && offsetof(Wear,crash)==56,
              "mirrored plugin field offsets changed");
struct Sample {
    uint64_t ns, interval, hud;uint32_t frame, gameMs;int reactions, helis;uint32_t flags;
    int heliRef[2],heliState[2],crewCount[2];uint32_t loadedMask;
    int npcRef,npcState,npcMode;float weight,balance,rootError,pelvisOffsetError;bool physicalRoot;
    CVector root;Vec3 pelvis,head,footL,footR;
};
std::array<Sample, kMaxSamples> gSamples;
size_t gSampleCount;
bool gSamplesFull;

uint64_t Now() { return mach_absolute_time() * gTimebase.numer / gTimebase.denom; }
void Log(const char *format, ...) {
    if (gLog.empty()) return;
    struct stat st{};
    if (stat(gLog.c_str(), &st) == 0 && st.st_size > off_t(kMaxLog)) {
        const std::string old = gLog + ".1";
        unlink(old.c_str()); rename(gLog.c_str(), old.c_str());
    }
    FILE *f = fopen(gLog.c_str(), "a");
    if (!f) return;
    fprintf(f, "[VCMM-PROBE %.3f] ", Now()/1e9);
    va_list args; va_start(args, format); vfprintf(f, format, args); va_end(args);
    fputc('\n', f); fclose(f);
}
std::string Quote(const std::string &text) {
    std::string out = "\"";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
        else if (c >= 32 && c < 127) out += char(c);
        else out += '?';
    }
    return out + '"';
}
bool AtomicWrite(const std::string &path, const std::string &value) {
    const std::string temporary = path + ".tmp." + std::to_string(getpid());
    const int fd = open(temporary.c_str(), O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW, 0600);
    if (fd < 0) return false;
    const char *p = value.data(); size_t left = value.size(); bool ok = true;
    while (left) { const ssize_t n = write(fd, p, left); if (n <= 0) { ok = false; break; } p += n; left -= size_t(n); }
    if (close(fd)) ok = false;
    if (ok) ok = rename(temporary.c_str(), path.c_str()) == 0;
    if (!ok) unlink(temporary.c_str());
    return ok;
}
const uint8_t *Uuid(const mach_header_64 *header) {
    const auto *cmd = reinterpret_cast<const load_command *>(header+1);
    for (uint32_t i=0; i<header->ncmds; ++i) {
        if (cmd->cmdsize < sizeof(load_command)) return nullptr;
        if (cmd->cmd == LC_UUID) return reinterpret_cast<const uuid_command *>(cmd)->uuid;
        cmd = reinterpret_cast<const load_command *>(reinterpret_cast<const uint8_t *>(cmd)+cmd->cmdsize);
    }
    return nullptr;
}
template <class T> bool Bind(T &out, const char *name) {
    out = reinterpret_cast<T>(dlsym(RTLD_MAIN_ONLY, name));
    if (!out) Log("missing native symbol: %s", name);
    return out != nullptr;
}
bool Resolve() {
    bool ok = true;
#define B(member, name) ok = Bind(g.member, name) && ok
    B(player, "_Z13FindPlayerPedv"); B(ped, "_ZN6CPools6GetPedEi"); B(vehicle, "_ZN6CPools10GetVehicleEi");
    B(pedRef, "_ZN6CPools9GetPedRefEP4CPed"); B(vehicleRef, "_ZN6CPools13GetVehicleRefEP8CVehicle");
    B(teleport,"_ZN4CPed8TeleportE7CVector"); B(collision,"_ZN10CStreaming18LoadSceneCollisionERK7CVector");
    B(scene,"_ZN10CStreaming9LoadSceneERK7CVector"); B(clock,"_ZN6CClock12SetGameClockEhh");
    B(weather,"_ZN8CWeather15ForceWeatherNowEs"); B(weatherRelease,"_ZN8CWeather14ReleaseWeatherEv");
    B(cameraPosition,"_ZN7CCamera26SetCamPositionForFixedModeERK7CVectorS2_");
    B(cameraTarget,"_ZN7CCamera19TakeControlNoEntityERK7CVectorsi"); B(cameraRestore,"_ZN7CCamera18RestoreWithJumpCutEv");
    B(componentPosition,"_ZN6CPedIK20GetComponentPositionERN2rw3V3dEj");
    B(request,"_ZN10CStreaming12RequestModelEii"); B(addPed,"_ZN11CPopulation6AddPedE8ePedTypejRK7CVectori");
    B(removePed,"_ZN11CPopulation9RemovePedEP4CPed"); B(ground,"_ZN6CWorld21FindGroundZFor3DCoordEfffPb");
    B(vertical,"_ZN6CWorld19ProcessVerticalLineERK7CVectorfR9CColPointRP7CEntitybbbbbbP15CStoredCollPoly");
    B(line,"_ZN6CWorld18ProcessLineOfSightERK7CVectorS2_R9CColPointRP7CEntitybbbbbbbb");
    B(damage,"_ZN4CPed13InflictDamageEP7CEntity11eWeaponTypef14ePedPieceTypesh"); B(wanted,"_ZN10CPlayerPed14SetWantedLevelEi");
    B(bulletImpact,"_ZN7CWeapon14DoBulletImpactEP7CEntityS1_P7CVectorS3_P9CColPoint9CVector2D");
    B(hierarchy,"_Z29GetAnimHierarchyFromSkinClumpPN2rw5ClumpE"); B(matrices,"_Z30RpHAnimHierarchyGetMatrixArrayPN2rw14HAnimHierarchyE"); B(boneIndex,"_Z17RpHAnimIDGetIndexPN2rw14HAnimHierarchyEi");
    B(weaponInfo,"_ZN11CWeaponInfo13GetWeaponInfoE11eWeaponType");
    // Verified int overload stores its argument directly at ped+0x64c and
    // indexes the 0x18-byte inventory slots. The enum overload maps type to slot.
    B(setWeaponSlot,"_ZN4CPed16SetCurrentWeaponEi");
    B(pad,"_ZN4CPad6GetPadEi");B(onMission,"_ZN11CTheScripts18IsPlayerOnAMissionEv");B(padAffected,"_ZN4CPad22IsAffectedByControllerE");
    B(vehiclePool,"_ZN6CPools15ms_pVehiclePoolE");
    B(updateMatrix,"_ZN7CMatrix8UpdateRWEv");B(updateFrame,"_ZN7CEntity13UpdateRwFrameEv");
    B(menu,"FrontEndMenuManager"); B(camera,"TheCamera"); B(stream,"_ZN10CStreaming16ms_aInfoForModelE");
    B(particleSystems,"mod_ParticleSystemManager"); B(particleArray,"gParticleArray"); B(particleInterval,"nParticleCreationInterval");
    B(rsGlobal,"RsGlobal");
    B(pedPool,"_ZN6CPools11ms_pPedPoolE"); B(helis,"_ZN5CHeli6pHelisE"); B(heliCount,"_ZN5CHeli14NumRandomHelisE");
    B(time,"_ZN6CTimer22m_snTimeInMillisecondsE"); B(frame,"_ZN6CTimer14m_FrameCounterE"); B(area,"_ZN5CGame8currAreaE");
    B(hour,"_ZN6CClock18ms_nGameClockHoursE"); B(minute,"_ZN6CClock20ms_nGameClockMinutesE"); B(seconds,"_ZN6CClock20ms_nGameClockSecondsE");
    B(clockTick,"_ZN6CClock17ms_nLastClockTickE"); B(freeze,"gbFreezeTime"); B(slow,"g_SlowMode");
    B(userPause,"_ZN6CTimer11m_UserPauseE"); B(codePause,"_ZN6CTimer11m_CodePauseE"); B(cutscene,"_ZN12CCutsceneMgr10ms_runningE");
    B(weatherOld,"_ZN8CWeather14OldWeatherTypeE"); B(weatherNew,"_ZN8CWeather14NewWeatherTypeE"); B(weatherForced,"_ZN8CWeather17ForcedWeatherTypeE");
    B(weatherInterpolation,"_ZN8CWeather18InterpolationValueE"); B(rain,"_ZN8CWeather4RainE"); B(clouds,"_ZN8CWeather13CloudCoverageE");
    B(timeScale,"_ZN6CTimer13ms_fTimeScaleE");
    B(rtEnabled,"_ZN10CRTShadows7EnabledEv"); B(rtActive,"_ZN10CRTShadows6ActiveEv");
    B(foreground,"_Z15IsForegroundAppv");
    B(sunX,"_ZN8CCoronas10SunScreenXE"); B(sunY,"_ZN8CCoronas10SunScreenYE");
    B(sunIndex,"_ZN10CTimeCycle20m_CurrentStoredValueE"); B(sunVectors,"_ZN10CTimeCycle13m_VectorToSunE");
    B(shadowStrength,"_ZN10CTimeCycle24m_nCurrentShadowStrengthE");
    B(ambient[0],"_ZN10CTimeCycle20m_fCurrentAmbientRedE"); B(ambient[1],"_ZN10CTimeCycle22m_fCurrentAmbientGreenE"); B(ambient[2],"_ZN10CTimeCycle21m_fCurrentAmbientBlueE");
    B(directional[0],"_ZN10CTimeCycle24m_fCurrentDirectionalRedE"); B(directional[1],"_ZN10CTimeCycle26m_fCurrentDirectionalGreenE"); B(directional[2],"_ZN10CTimeCycle25m_fCurrentDirectionalBlueE");
    B(models,"_ZN10CModelInfo16ms_modelInfoPtrsE"); B(lights,"_ZN12CPointLights7aLightsE"); B(lightCount,"_ZN12CPointLights9NumLightsE");
    B(pipeEnable[0],"_ZN11CustomPipes14LightmapEnableE"); B(pipeMult[0],"_ZN11CustomPipes12LightmapMultE");
    B(pipeEnable[1],"_ZN11CustomPipes11GlossEnableE"); B(pipeMult[1],"_ZN11CustomPipes9GlossMultE");
    B(pipeEnable[2],"_ZN11CustomPipes14RimlightEnableE"); B(pipeMult[2],"_ZN11CustomPipes12RimlightMultE");
    B(filterMode,"_ZN7CPostFX12EffectSwitchE"); B(filterIntensity,"_ZN7CPostFX9IntensityE");
    B(association,"_Z30RpAnimBlendClumpGetAssociationPN2rw5ClumpEj");
    B(objectiveAt,"_ZN4CPed12SetObjectiveE10eObjective7CVector");
    B(blur[0],"_ZN10CTimeCycle17m_fCurrentBlurRedE"); B(blur[1],"_ZN10CTimeCycle19m_fCurrentBlurGreenE"); B(blur[2],"_ZN10CTimeCycle18m_fCurrentBlurBlueE");
    B(extraColourOn,"_ZN10CTimeCycle16m_bExtraColourOnE"); B(extraColour,"_ZN10CTimeCycle13m_ExtraColourE"); B(extraColourInterpolation,"_ZN10CTimeCycle18m_ExtraColourInterE");
#undef B
    g.cocoaWindow=reinterpret_cast<decltype(g.cocoaWindow)>(dlsym(RTLD_DEFAULT,"glfwGetCocoaWindow"));
    g.windowShouldClose=reinterpret_cast<decltype(g.windowShouldClose)>(dlsym(RTLD_DEFAULT,"glfwWindowShouldClose"));
    g.joystickPresent=reinterpret_cast<decltype(g.joystickPresent)>(dlsym(RTLD_DEFAULT,"glfwJoystickPresent"));
    g.joystickIsGamepad=reinterpret_cast<decltype(g.joystickIsGamepad)>(dlsym(RTLD_DEFAULT,"glfwJoystickIsGamepad"));
    g.gamepadState=reinterpret_cast<decltype(g.gamepadState)>(dlsym(RTLD_DEFAULT,"glfwGetGamepadState"));
    g.joystickName=reinterpret_cast<decltype(g.joystickName)>(dlsym(RTLD_DEFAULT,"glfwGetJoystickName"));
    g.joystickAxes=reinterpret_cast<decltype(g.joystickAxes)>(dlsym(RTLD_DEFAULT,"glfwGetJoystickAxes"));
    gStyleSelector=sel_registerName("styleMask");gToggleSelector=sel_registerName("toggleFullScreen:");
    return ok;
}
void PluginGlobals() {
    if(!kProbeHiddenEnabled) { Log("native-only probe: all plugin-local globals disabled");return; }
    for (uint32_t i=0; i<_dyld_image_count(); ++i) {
        const auto *header = reinterpret_cast<const mach_header_64 *>(_dyld_get_image_header(i));
        const char *path = _dyld_get_image_name(i);
        if (!path || !strstr(path, "libVCModernHUD.dylib")) continue;
        const uint8_t *uuid = Uuid(header);
        if (!uuid || memcmp(uuid, kProbePluginUuid, 16)) { Log("plugin UUID differs from generated offsets; plugin globals disabled"); return; }
        gHiddenMatched=true;
        const intptr_t slide = _dyld_get_image_vmaddr_slide(i);
        if (kProbePeopleAddress) gPeople = reinterpret_cast<const uint8_t *>(slide+kProbePeopleAddress);
        if (kProbeGradeAddress) {
            gGrade = reinterpret_cast<uint8_t *>(slide+kProbeGradeAddress);
            // This observer runs after plugin constructors. A false readiness
            // can mean grade::Install skipped its prerequisite bindings entirely.
            gGradeInitiallyReady = *gGrade != 0;
        }
        if (kProbeWantedAddress) gWanted = reinterpret_cast<const uint8_t *>(slide+kProbeWantedAddress);
        if (kProbeEyesAddress) gEyes = reinterpret_cast<const uint8_t *>(slide+kProbeEyesAddress);
        if (kProbeWearAddress) gWear = reinterpret_cast<const uint8_t *>(slide+kProbeWearAddress);
        if (kProbeCrewsAddress) gCrews = reinterpret_cast<const uint8_t *>(slide+kProbeCrewsAddress);
        if (kProbeConfigAddress) gClouds = reinterpret_cast<uint8_t *>(slide+kProbeConfigAddress+offsetof(Config,clouds));
        if (kProbeConfigAddress) gPlanesEnabled = reinterpret_cast<const uint8_t *>(slide+kProbeConfigAddress+offsetof(Config,planes));
        if (kProbePlaneForcesReadyAddress) gPlaneForcesReady=reinterpret_cast<const uint8_t *>(slide+kProbePlaneForcesReadyAddress);
        if (kProbePlaneRosterReadyAddress) gPlaneRosterReady=reinterpret_cast<const uint8_t *>(slide+kProbePlaneRosterReadyAddress);
        if (kProbePlaneAttemptAddress) gPlaneAttempt=reinterpret_cast<const uint32_t *>(slide+kProbePlaneAttemptAddress);
        if (kProbeAirportReadyAddress) gAirportReady=reinterpret_cast<const uint8_t *>(slide+kProbeAirportReadyAddress);
        if (kProbeAirportModelAddress) gAirportModel=reinterpret_cast<const int32_t *>(slide+kProbeAirportModelAddress);
        if (kProbeAirportShapeReadyAddress) gAirportShapeReady=reinterpret_cast<const uint8_t *>(slide+kProbeAirportShapeReadyAddress);
        if (kProbeJetWheelSnapshotAddress) gJetWheelSnapshot=reinterpret_cast<WheelSnapshot>(slide+kProbeJetWheelSnapshotAddress);
        if (kProbeJetGearDownAddress) gJetGearDown=reinterpret_cast<decltype(gJetGearDown)>(slide+kProbeJetGearDownAddress);
        if (kProbeJetEnginePowerAddress) gJetEnginePower=reinterpret_cast<decltype(gJetEnginePower)>(slide+kProbeJetEnginePowerAddress);
        if (kProbeConfigAddress) gFirstPerson=reinterpret_cast<const uint8_t *>(slide+kProbeConfigAddress+offsetof(Config,firstPerson));
        if (kProbeCameraCockpitAddress) gCameraCockpit=reinterpret_cast<decltype(gCameraCockpit)>(slide+kProbeCameraCockpitAddress);
        if (kProbeCameraFirstPersonAddress) gCameraFirstPerson=reinterpret_cast<const uint8_t *>(slide+kProbeCameraFirstPersonAddress);
        if (kProbeCameraHeadReadyAddress) gCameraHeadReady=reinterpret_cast<const uint8_t *>(slide+kProbeCameraHeadReadyAddress);
        if (kProbeCameraLookPitchAddress) gCameraLookPitch=reinterpret_cast<const float *>(slide+kProbeCameraLookPitchAddress);
        if (kProbeCameraLookYawAddress) gCameraLookYaw=reinterpret_cast<const float *>(slide+kProbeCameraLookYawAddress);
        if (kProbeCameraHeadAboutAddress) gCameraHeadAbout=reinterpret_cast<decltype(gCameraHeadAbout)>(slide+kProbeCameraHeadAboutAddress);
        if (kProbeCameraHeadXAddress) gCameraHeadX=reinterpret_cast<const float *>(slide+kProbeCameraHeadXAddress);
        if (kProbeCameraHeadYAddress) gCameraHeadY=reinterpret_cast<const float *>(slide+kProbeCameraHeadYAddress);
        if (kProbeCameraHeadZAddress) gCameraHeadZ=reinterpret_cast<const float *>(slide+kProbeCameraHeadZAddress);
        if (kProbePlanePreviousAddress) gPlanePrevious=reinterpret_cast<const uint32_t *>(slide+kProbePlanePreviousAddress);
        if (kProbeNeckAddress) gPhysicalNeck = reinterpret_cast<BodyPoint>(slide+kProbeNeckAddress);
        if (kProbeHeadAxisAddress) gPhysicalHeadAxis = reinterpret_cast<BodyPoint>(slide+kProbeHeadAxisAddress);
        Log("plugin UUID matched; reaction stride=%zu, hidden globals enabled", sizeof(ReactionPerson));
        return;
    }
}
int ReactionCount() {
    if (!gPeople) return -1;
    const uint8_t *begin = nullptr, *end = nullptr;
    memcpy(&begin,gPeople,sizeof begin); memcpy(&end,gPeople+sizeof begin,sizeof end);
    const uintptr_t a = reinterpret_cast<uintptr_t>(begin), b = reinterpret_cast<uintptr_t>(end);
    if (!a && !b) return 0;
    if (!a || b<a || (b-a)%sizeof(ReactionPerson) || (b-a)/sizeof(ReactionPerson)>12) return -1;
    return int((b-a)/sizeof(ReactionPerson));
}
void ForgetOwned() { gOwned = -1; gOwnedPointer = nullptr; }
uint8_t *Owned() {
    if (gOwned < 0) return nullptr;
    uint8_t *ped = g.ped(gOwned);
    if (ped != gOwnedPointer) { ForgetOwned(); return nullptr; }
    return ped;
}
template <class V> std::string VectorJson(V v) {
    if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))return "null";
    char out[160];snprintf(out,sizeof out,"[%.6f,%.6f,%.6f]",double(v.x),double(v.y),double(v.z));return out;
}
std::string InputSourcesJson(const uint8_t *pad){
    auto state=[&](size_t offset){std::string out="[";for(int i=0;i<21;++i){if(i)out+=",";out+=std::to_string(*reinterpret_cast<const int16_t *>(pad+offset+size_t(i)*2));}return out+"]";};
    std::string out="{\"temp_6c\":"+(pad?state(0x6c):"null")+",\"joy_96\":"+(pad?state(0x96):"null")+",\"temp_c0\":"+(pad?state(0xc0):"null");
    const uint8_t *platform=g.rsGlobal?*reinterpret_cast<uint8_t *const *>(g.rsGlobal+0x20):nullptr;
    out+=",\"selected_joystick\":"+std::to_string(platform?int(*reinterpret_cast<const int8_t *>(platform+0x21)):-1)+",\"devices\":[";
    struct GamepadState{unsigned char buttons[15];float axes[6];};
    static_assert(sizeof(GamepadState)==40&&offsetof(GamepadState,axes)==16,"GLFW gamepad ABI");
    bool comma=false;
    for(int id=0;id<16;++id){
        if(!g.joystickPresent||!g.joystickPresent(id))continue;if(comma)out+=",";comma=true;
        const char *name=g.joystickName?g.joystickName(id):nullptr;
        out+="{\"id\":"+std::to_string(id)+",\"name\":"+Quote(name?name:"")+",\"gamepad\":"+std::to_string(g.joystickIsGamepad?g.joystickIsGamepad(id):0);
        GamepadState mapped{};
        if(g.gamepadState&&g.gamepadState(id,&mapped)){
            out+=",\"mapped_axes\":[";for(int i=0;i<6;++i){if(i)out+=",";out+=std::isfinite(mapped.axes[i])?std::to_string(mapped.axes[i]):"null";}out+="]";
        }
        int count=0;const float *axes=g.joystickAxes?g.joystickAxes(id,&count):nullptr;
        out+=",\"raw_axes\":[";if(axes&&count>0&&count<=32)for(int i=0;i<count;++i){if(i)out+=",";out+=std::isfinite(axes[i])?std::to_string(axes[i]):"null";}out+="]}";
    }
    return out+"]}";
}
std::string WheelsJson(const uint8_t *vehicle) {
    std::array<jetwheels::WheelPose,3> wheels{};
    if(!gJetWheelSnapshot||!gJetWheelSnapshot(vehicle,wheels))return "null";
    std::string out="[";
    for(size_t i=0;i<wheels.size();++i){
        if(i)out+=",";const auto &wheel=wheels[i];
        auto number=[](float value){return std::isfinite(value)?std::to_string(value):"null";};
        out+="{\"grounded\":"+std::string(wheel.grounded?"true":"false")+",\"spin_radians\":"+number(wheel.spin)+
            ",\"compression_m\":"+number(wheel.compression)+",\"steer_radians\":"+number(wheel.steer)+"}";
    }
    return out+"]";
}
float Distance(CVector a,CVector b) { const float x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;return std::sqrt(x*x+y*y+z*z); }
// Supported native CParticleSystemData/CParticle layouts, checked against
// AddParticle, Update and Render. Read only on the same game/HUD thread.
std::string ExhaustParticlesJson() {
    if(!g.particleSystems||!g.particleArray||!g.particleInterval||!g.camera)return "null";
    const uint8_t *entry=g.particleSystems+67*0xa0;
    if(*reinterpret_cast<const int32_t *>(entry)!=67||std::memcmp(entry+4,"EXHAUST_FUMES",14))return "null";
    const uintptr_t begin=reinterpret_cast<uintptr_t>(g.particleArray),end=begin+750*0x60;
    const uint8_t *node=*reinterpret_cast<const uint8_t *const *>(entry+0x98);
    std::array<bool,750> seen{};size_t count=0;bool valid=true;int maxAlpha=0;
    const CVector camera=*reinterpret_cast<const CVector *>(g.camera+0x30);
    float nearest=std::numeric_limits<float>::infinity();std::string samples="[";
    while(node&&count<750){
        const uintptr_t address=reinterpret_cast<uintptr_t>(node);
        if(address<begin||address>=end||(address-begin)%0x60){valid=false;break;}
        const size_t index=(address-begin)/0x60;if(seen[index]){valid=false;break;}seen[index]=true;
        const CVector at=*reinterpret_cast<const CVector *>(node);
        const float radius=*reinterpret_cast<const float *>(node+0x3c),distance=Distance(at,camera);
        if(std::isfinite(distance))nearest=std::min(nearest,distance);
        maxAlpha=std::max(maxAlpha,int(node[0x3b]));
        if(count<6){if(count)samples+=",";samples+="{\"position\":"+VectorJson(at)+",\"alpha\":"+std::to_string(node[0x3b])+
            ",\"intensity\":"+std::to_string(node[0x3a])+",\"radius\":"+(std::isfinite(radius)?std::to_string(radius):"null")+
            ",\"expiry_ms\":"+std::to_string(*reinterpret_cast<const uint32_t *>(node+0x18))+"}";}
        ++count;node=*reinterpret_cast<const uint8_t *const *>(node+0x58);
    }
    if(node&&count==750)valid=false;
    const float range=*reinterpret_cast<const float *>(entry+0x18);
    return "{\"valid\":"+std::string(valid?"true":"false")+",\"live\":"+std::to_string(count)+",\"max_alpha\":"+std::to_string(maxAlpha)+
        ",\"nearest_camera_m\":"+(std::isfinite(nearest)?std::to_string(nearest):"null")+
        ",\"create_range_squared\":"+(std::isfinite(range)?std::to_string(range):"null")+
        ",\"creation_interval\":"+std::to_string(*g.particleInterval)+",\"samples\":"+samples+"]}";
}
CVector WorldPoint(uint8_t *entity,CVector local) {
    const CVector right=*reinterpret_cast<CVector *>(entity+8),forward=*reinterpret_cast<CVector *>(entity+0x18),up=*reinterpret_cast<CVector *>(entity+0x28),pos=game::EntityPos(entity);
    return {pos.x+right.x*local.x+forward.x*local.y+up.x*local.z,pos.y+right.y*local.x+forward.y*local.y+up.y*local.z,pos.z+right.z*local.x+forward.z*local.y+up.z*local.z};
}
void *CurrentCocoaWindow() {
    if(!pthread_main_np() || !gSymbols || !g.rsGlobal || !g.cocoaWindow || !g.windowShouldClose ||
       *reinterpret_cast<const int32_t *>(g.rsGlobal+0x1c))return nullptr;
    const auto *platform=*reinterpret_cast<uint8_t *const *>(g.rsGlobal+0x20);
    void *window=platform?*reinterpret_cast<void *const *>(platform):nullptr;
    return window&&!g.windowShouldClose(window)?g.cocoaWindow(window):nullptr;
}
uintptr_t NativeWindowStyle(void *window) {
    return reinterpret_cast<uintptr_t (*)(id,SEL)>(objc_msgSend)(reinterpret_cast<id>(window),gStyleSelector);
}
void ToggleNativeWindow(void *window) {
    reinterpret_cast<void (*)(id,SEL,id)>(objc_msgSend)(reinterpret_cast<id>(window),gToggleSelector,nil);
}
std::string WindowJson() {
    void *window=CurrentCocoaWindow();
    const int renderer=g.menu?*reinterpret_cast<const int32_t *>(g.menu+0x718):-1;
    if(!window)return "{\"available\":false,\"native_fullscreen\":null,\"renderer_windowed\":"+std::to_string(renderer)+"}";
    const uintptr_t style=NativeWindowStyle(window);const bool fullscreen=(style&(uintptr_t(1)<<14))!=0;
    if(gWindowTransition.owner!=window || (gWindowTransition.desired>=0 && fullscreen==bool(gWindowTransition.desired)) ||
       Now()-gWindowTransition.since>=3000000000ULL)gWindowTransition={};
    return "{\"available\":true,\"native_fullscreen\":"+std::string(fullscreen?"true":"false")+
        ",\"renderer_windowed\":"+std::to_string(renderer)+",\"style_mask\":"+std::to_string(style)+
        ",\"pending_native\":"+(gWindowTransition.desired>=0?std::to_string(gWindowTransition.desired):"null")+"}";
}
std::string PlayerBonesJson(uint8_t *player) {
    if(!player || g.ped(g.pedRef(player))!=player)return "null";
    uint8_t *clump=game::EntityClump(player),*hier=clump?g.hierarchy(clump):nullptr;
    const float *matrices=hier?g.matrices(hier):nullptr;if(!matrices)return "null";
    std::string out="{\"right\":"+VectorJson(*reinterpret_cast<const CVector *>(player+8))+
        ",\"forward\":"+VectorJson(game::EntityForward(player))+",\"joints\":{";
    struct Joint { const char *name;int tag; };
    bool comma=false;
    for(const Joint &joint:{Joint{"chest",3},{"neck",4},{"shoulder_l",32},{"elbow_l",33},{"wrist_l",34},
                            {"shoulder_r",22},{"elbow_r",23},{"wrist_r",24}}) {
        if(comma)out+=",";comma=true;const int index=g.boneIndex(hier,joint.tag);
        out+=Quote(joint.name)+":"+(index>=0&&index<128?VectorJson(*reinterpret_cast<const CVector *>(matrices+size_t(index)*16+12)):"null");
    }
    return out+"}}";
}
const ReactionPerson *ReactionOf(uint8_t *ped) {
    const int count=ReactionCount();if(!ped||count<=0)return nullptr;
    const ReactionPerson *begin=nullptr;memcpy(&begin,gPeople,sizeof begin);
    for(int i=0;i<count;++i)if(begin[i].handle==gOwned && g.ped(begin[i].handle)==ped && begin[i].clump==game::EntityClump(ped))return begin+i;
    return nullptr;
}
uint8_t *RandomHeli(int slot) {
    if(slot<0||slot>1)return nullptr;
    uint8_t *heli=g.helis[slot];
    return heli && g.vehicle(g.vehicleRef(heli))==heli && game::EntityModel(heli)==165 && heli[0x3b1]==0?heli:nullptr;
}
const Crew *CrewOf(int slot,uint8_t *heli) {
    if(!gCrews||!heli||slot<0||slot>1)return nullptr;
    const Crew *crew=reinterpret_cast<const Crew *>(gCrews)+slot;
    return crew->heli==g.vehicleRef(heli)?crew:nullptr;
}
const Wear *WearOf(int slot,uint8_t *heli) {
    if(!gWear||kProbeWearStride!=sizeof(Wear)||!heli||slot<0||slot>1)return nullptr;
    const Wear *wear=reinterpret_cast<const Wear *>(gWear)+slot;
    return wear->handle==g.vehicleRef(heli)?wear:nullptr;
}
uint8_t *PilotOf(int slot,uint8_t *heli) {
    const Crew *crew=CrewOf(slot,heli);if(!crew||crew->pilot<0||crew->pilotShot)return nullptr;
    uint8_t *ped=g.ped(crew->pilot);
    return ped && *reinterpret_cast<uint8_t **>(ped+0x720)==heli && game::EntityModel(ped)==1 && *reinterpret_cast<int32_t *>(ped+0x4f8)==4 && game::PedFromMission(ped)?ped:nullptr;
}
std::string PedJson(int ref,uint8_t *attached) {
    uint8_t *ped=ref>=0?g.ped(ref):nullptr;if(!ped)return "null";
    char text[256];snprintf(text,sizeof text,"{\"ref\":%d,\"model\":%d,\"type\":%d,\"health\":%.5f,\"state\":%d,\"attached\":%s,\"position\":",ref,int(game::EntityModel(ped)),*reinterpret_cast<int32_t *>(ped+0x4f8),double(game::Health(ped)),game::PedState(ped),*reinterpret_cast<uint8_t **>(ped+0x720)==attached?"true":"false");
    return std::string(text)+VectorJson(game::EntityPos(ped))+"}";
}
std::string ApbJson(uint8_t *player) {
    if(!player||!gWanted)return "null";
    const auto &m=*reinterpret_cast<const WantedModel *>(gWanted);
    if(int(m.state)>4||m.stars<0||m.stars>6||m.escaped<0||m.escaped>6||m.description.outfit.size()>64)return "null";
    char outfit[25]{};if(g.models[0])memcpy(outfit,g.models[0]+8,24);
    uint8_t *vehicle=game::PedInVehicle(player)?game::PedVehicle(player):nullptr;
    if(vehicle && g.vehicle(g.vehicleRef(vehicle))!=vehicle)vehicle=nullptr;
    char text[1024];snprintf(text,sizeof text,"{\"state\":%d,\"stars\":%d,\"escaped\":%d,\"calling\":%s,\"have_outfit\":%s,\"have_vehicle\":%s,\"vehicle_model\":%d,\"vehicle_colours\":[%d,%d],\"vehicle_ref\":%d,\"last_seen_at\":%u,\"search_since\":%u,\"current_vehicle_model\":%d,\"current_vehicle_colours\":[%d,%d]",int(m.state),m.stars,m.escaped,m.calling?"true":"false",m.description.haveOutfit?"true":"false",m.description.haveVehicle?"true":"false",m.description.vehicleModel,m.description.colour1,m.description.colour2,m.description.vehicle,m.lastSeenAt,m.searchSince,vehicle?int(game::EntityModel(vehicle)):-1,vehicle?int(vehicle[0x210]):-1,vehicle?int(vehicle[0x211]):-1);
    std::string out=text;out+=",\"outfit\":"+Quote(m.description.outfit)+",\"current_outfit\":"+Quote(outfit)+",\"last_seen\":"+VectorJson(m.lastSeen)+",\"search_centre\":"+VectorJson(m.searchCentre);
    int eyes=0,spotted=0;float maxFill=0,maxUnspotted=0;
    if(gEyes) {
        const sight::Eye *begin=nullptr,*end=nullptr;memcpy(&begin,gEyes,sizeof begin);memcpy(&end,gEyes+sizeof begin,sizeof end);
        const uintptr_t a=reinterpret_cast<uintptr_t>(begin),b=reinterpret_cast<uintptr_t>(end);
        if(a&&b>=a&&(b-a)%sizeof(sight::Eye)==0&&(b-a)/sizeof(sight::Eye)<=256)for(const sight::Eye *e=begin;e<end;++e) { ++eyes;spotted+=e->spotted;maxFill=std::max(maxFill,e->fill);if(!e->spotted)maxUnspotted=std::max(maxUnspotted,e->fill); }
    }
    snprintf(text,sizeof text,",\"eyes\":%d,\"spotted_eyes\":%d,\"max_eye_fill\":%.6f,\"max_unspotted_eye_fill\":%.6f}",eyes,spotted,double(maxFill),double(maxUnspotted));return out+text;
}
std::string NpcPoseJson(uint8_t *ped) {
    if(!ped)return "null";
    uint8_t *clump=game::EntityClump(ped),*hier=clump?g.hierarchy(clump):nullptr;float *matrices=hier?g.matrices(hier):nullptr;
    const ReactionPerson *p=ReactionOf(ped);
    char text[768];snprintf(text,sizeof text,"{\"reaction_present\":%s,\"mode\":%d,\"native_mode\":%d,\"active\":%s,\"dead\":%s,\"weight\":%.6f,\"balance\":%.6f,\"last_steps\":%d,\"hit_at\":%u,\"stepped_at\":%u,\"published_at\":%u,\"saved_pose\":%s,\"held_fall\":%s,\"native_getup_timer\":%u,\"root\":",p?"true":"false",p?int(p->physical.State()):-1,p?int(p->nativeMode):-1,p&&p->physical.Active()?"true":"false",p&&p->physical.Dead()?"true":"false",p?double(p->physical.Weight()):0,p?double(p->physical.Balance()):0,p?p->physical.LastSteps():0,p?p->hitAt:0,p?p->stepped:0,p?p->publishedAt:0,p&&p->saved?"true":"false",p&&p->heldFall?"true":"false",*reinterpret_cast<uint32_t *>(ped+0x68c));
    std::string out=text;out+=VectorJson(game::EntityPos(ped));
    const bool physicalRoot=p&&probe::PhysicalRoot(p->physical.Active(),p->physical.State());
    out+=",\"root_owner\":"+Quote(physicalRoot?"physical":"native");
    if(p) {
        const Vec3 pelvis=p->physical.Positions()[reactphysics::Pelvis];const CVector expected{pelvis.x-p->pelvisOffset.x,pelvis.y-p->pelvisOffset.y,pelvis.z-p->pelvisOffset.z};
        if(physicalRoot) { snprintf(text,sizeof text,",\"root_error\":%.6f",double(Distance(expected,game::EntityPos(ped))));out+=text; }
        else out+=",\"root_error\":null";
        out+=",\"root_expected\":"+(physicalRoot?VectorJson(expected):"null")+",\"published_velocity\":"+VectorJson(p->publishedVelocity);
        const CVector root=game::EntityPos(ped);const Vec3 actual{pelvis.x-root.x,pelvis.y-root.y,pelvis.z-root.z};
        out+=",\"pelvis_offset\":"+VectorJson(p->pelvisOffset)+",\"pelvis_from_root\":"+VectorJson(actual);
        snprintf(text,sizeof text,",\"pelvis_offset_error\":%.6f",double(Distance(expected,root)));out+=text;
        out+=",\"physical_neck\":"+(gPhysicalNeck?VectorJson(gPhysicalNeck(&p->physical)):"null");
        out+=",\"physical_head_axis\":"+(gPhysicalHeadAxis?VectorJson(gPhysicalHeadAxis(&p->physical)):"null");
    }
    out+=",\"native_flags\":{";
    bool comma=false;
    for(size_t offset:{0x60,0x64,0x188,0x18c,0x190,0x194}) {
        if(comma)out+=",";comma=true;char key[24];snprintf(key,sizeof key,"0x%zx",offset);
        out+=Quote(key)+":"+std::to_string(*reinterpret_cast<uint32_t *>(ped+offset));
    }
    out+="},\"getup_animations\":[";
    for(int id:{137,140}) {
        if(id!=137)out+=",";uint8_t *association=clump?g.association(clump,uint32_t(id)):nullptr;
        if(!association) { out+="null";continue; }
        const float current=*reinterpret_cast<float *>(association+0x30),speed=*reinterpret_cast<float *>(association+0x34),step=*reinterpret_cast<float *>(association+0x38);
        const uint8_t *hierarchy=*reinterpret_cast<uint8_t **>(association+0x20);const float length=hierarchy?*reinterpret_cast<const float *>(hierarchy+0x24):0;
        if(!std::isfinite(current)||!std::isfinite(speed)||!std::isfinite(step)||!std::isfinite(length)) { out+="null";continue; }
        snprintf(text,sizeof text,"{\"id\":%d,\"current_time\":%.6f,\"speed\":%.6f,\"time_step\":%.6f,\"flags\":%u,\"length\":%.6f}",id,double(current),double(speed),double(step),unsigned(*reinterpret_cast<uint16_t *>(association+0x3e)),double(length));out+=text;
    }
    out+="],\"bones\":{";
    for(size_t b=0;b<=probe::kJoints.size()+1;++b) {
        const bool neck=b==probe::kJoints.size()+1;
        const int joint=b&&!neck?int(probe::kJoints[b-1].joint):-1;
        const int tag=neck?4:b?probe::kJoints[b-1].tag:0;
        const char *name=neck?"neck":b?probe::kJoints[b-1].name:"root";
        if(b)out+=",";out+=Quote(name)+":{";
        const int index=hier?g.boneIndex(hier,tag):-1;
        const bool rendered=matrices&&index>=0&&index<128;
        CVector render{};if(rendered)render=*reinterpret_cast<CVector *>(matrices+size_t(index)*16+12);
        out+="\"rendered\":"+(rendered?VectorJson(render):"null");
        auto basis = [](const float *m) {
            if(!m)return std::string("null");std::string result="[";char values[160];
            for(int row=0;row<3;++row) {
                for(int col=0;col<3;++col)if(!std::isfinite(m[row*4+col]))return std::string("null");
                if(row)result+=",";snprintf(values,sizeof values,"[%.6f,%.6f,%.6f]",double(m[row*4]),double(m[row*4+1]),double(m[row*4+2]));result+=values;
            }
            return result+"]";
        };
        out+=",\"rendered_axes\":"+basis(rendered?matrices+size_t(index)*16:nullptr);
        if(p&&joint>=0) {
            out+=",\"physical\":"+VectorJson(p->physical.Positions()[size_t(joint)])+",\"velocity\":"+VectorJson(p->physical.Velocity(reactphysics::Joint(joint)));
        } else out+=",\"physical\":null,\"velocity\":null";
        const int original=p?(joint>=0?p->joints[size_t(joint)]:neck?4:0):-1;
        const float *native=p&&original>=0&&original<body::kBones?p->original[size_t(original)].data():nullptr;
        out+=",\"native_target\":"+(native?VectorJson(CVector{native[12],native[13],native[14]}):"null");
        out+=",\"native_axes\":"+basis(native);
        if(rendered && (joint==reactphysics::FootL||joint==reactphysics::FootR) && std::isfinite(render.x) && std::isfinite(render.y) && std::isfinite(render.z)) {
            alignas(16) uint8_t point[0x30]{};uint8_t *entity=nullptr;const CVector from{render.x,render.y,render.z+0.5f};
            const bool hit=g.vertical(from,render.z-3,point,entity,true,false,false,true,true,false,nullptr);
            out+=",\"ground_hit\":"+std::string(hit?"true":"false");if(hit)out+=",\"ground\":"+VectorJson(*reinterpret_cast<CVector *>(point))+",\"ground_normal\":"+VectorJson(*reinterpret_cast<CVector *>(point+0x10));
        }
        out+="}";
    }
    return out+"}}";
}
std::string HeliDetails(int slot,uint8_t *heli) {
    std::string out=",\"velocity\":"+VectorJson(*reinterpret_cast<CVector *>(heli+0x84));
    out+=",\"kind\":"+std::to_string(unsigned(heli[0x3b1]))+",\"breakup_at\":"+std::to_string(*reinterpret_cast<uint32_t *>(heli+0x39c));
    const Crew *crew=CrewOf(slot,heli);
    if(crew)out+=",\"crew\":{\"pilot_shot\":"+std::string(crew->pilotShot?"true":"false")+",\"pilot\":"+PedJson(crew->pilot,heli)+",\"gunners\":["+PedJson(crew->ped[0],heli)+","+PedJson(crew->ped[1],heli)+"]}";
    else out+=",\"crew\":null";
    const Wear *w=WearOf(slot,heli);
    if(w) {
        char text[768];snprintf(text,sizeof text,",\"wear\":{\"lamp_hits\":%d,\"rotor_hits\":%d,\"lamp_out\":%s,\"rotor_out\":%s,\"actual_light\":%.6f,\"crash_phase\":%d,\"crash_cause\":%d,\"crash_elapsed\":%.6f,\"crash_fallen\":%.6f,\"arrival_at\":%u,\"setup_samples\":%u,\"setup_max_ms\":%.6f,\"setup_total_ms\":%.6f,\"crash_velocity\":",w->lampHits,w->rotorHits,w->lampOut?"true":"false",w->rotorOut?"true":"false",double(w->light),int(w->crash.phase),int(w->crash.cause),double(w->crash.elapsed),double(w->crash.fallen),w->arrivalAt,w->setupSamples,w->setupMaxMs,w->setupTotalMs);
        out+=text;out+=VectorJson(w->crash.velocity)+",\"spot\":"+VectorJson(w->spot)+"}";
        // Same optical lens position as helizones::Lens, without mutating it.
        const Vec3 &mid=w->lamp.middle,&dir=w->lamp.forward;const CVector local{mid.x+dir.x*(helizones::kLampLength*.5f-.012f),mid.y+dir.y*(helizones::kLampLength*.5f-.012f),mid.z+dir.z*(helizones::kLampLength*.5f-.012f)};
        const CVector lens=WorldPoint(heli,local);int emitted=0;float maximum=0;
        if(*g.lightCount>=0&&*g.lightCount<=64)for(int i=0;i<*g.lightCount;++i) {
            const uint8_t *light=g.lights+size_t(i)*0x2c;
            if(light[0x28]!=1||Distance(*reinterpret_cast<const CVector *>(light),lens)>1.0f)continue;
            ++emitted;maximum=std::max(maximum,*reinterpret_cast<const float *>(light+0x1c));
        }
        out+=",\"optical_lens\":"+VectorJson(lens)+",\"emitted_spots\":"+std::to_string(emitted)+",\"emitted_red_max\":"+std::to_string(maximum);
    } else out+=",\"wear\":null";
    return out;
}
uint8_t *ReadyPlayer() {
    // FrontEndMenuManager::m_bGameNotLoaded, CPools, CTimer, and area are
    // checked before the first native function is called, including FindPlayerPed.
    if (!gSymbols || !g.pedPool || !*g.pedPool || g.menu[0x6c] || *g.frame<30 || !*g.time || *g.area<0 || *g.area>20) return nullptr;
    uint8_t *ped = g.player();
    if (!ped || (ped[0x60]&7)!=3) return nullptr;
    const int ref = g.pedRef(ped);
    if (ref<0 || g.ped(ref)!=ped) return nullptr;
    const CVector at = game::EntityPos(ped);
    if (!std::isfinite(at.x) || !std::isfinite(at.y) || !std::isfinite(at.z)) return nullptr;
    return ped;
}
bool WorldAllowed(uint8_t *player,bool estateExit=false) {
    if (!player || (*g.area!=0&&!estateExit) || g.menu[0x38] || *g.cutscene || *g.userPause || *g.codePause) {
        gResult="requires active outdoor gameplay, no menu/cutscene/pause"; return false;
    }
    return true;
}
bool Eligible(uint8_t *ped) {
    return ped && !game::PedFromMission(ped) && !game::PedInVehicle(ped) &&
           !*reinterpret_cast<void **>(ped+0x720) && *reinterpret_cast<int32_t *>(ped+0x4f8)==4 &&
           (game::PedState(ped)<0x30 || game::PedState(ped)==game::kPedDie || game::PedState(ped)==game::kPedDead);
}
void Originals(uint8_t *player) {
    // Process-global toggles survive world loads. Their baseline must survive
    // too, even when the player/clock/weather snapshot belongs to a new world.
    if (!gToggleOriginals) {
        gToggleOriginals=true;
        gOriginalRt=g.menu[0x72a];gOriginalGrade=gGrade?*gGrade:0;
        gOriginalClouds=gClouds?*gClouds:0;
        gOriginalFilterMode=*g.filterMode;gOriginalFilterIntensity=*g.filterIntensity;
        for(int i=0;i<3;++i) { gOriginalPipeEnable[i]=*g.pipeEnable[i];gOriginalPipeMult[i]=*g.pipeMult[i]; }
    }
    if (gOriginals) return;
    gOriginals = true;
    gOriginalFreeze=*g.freeze;
    gOriginalHour=*g.hour; gOriginalMinute=*g.minute;
    gOriginalWeatherOld=*g.weatherOld; gOriginalWeatherNew=*g.weatherNew; gOriginalWeatherForced=*g.weatherForced;
    probe::CaptureWeapon(player,g.pedRef(player),g.ped,gWeaponSnapshot);
}
std::string PointLightsJson() {
    const int count=g.lightCount?int(*g.lightCount):-1;
    std::string out="{\"native_count\":"+std::to_string(count)+",\"entries\":[";
    bool first=true;
    if(g.lights&&count>=0&&count<=64)for(int i=0;i<count;++i){
        const uint8_t *p=g.lights+size_t(i)*0x2c;CVector position{},direction{},colour{};float radius;
        std::memcpy(&position,p,12);std::memcpy(&direction,p+0xc,12);std::memcpy(&radius,p+0x18,4);std::memcpy(&colour,p+0x1c,12);
        auto finite=[](CVector v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);};
        if(!finite(position)||!finite(direction)||!finite(colour)||!std::isfinite(radius))continue;
        if(!first)out+=",";first=false;
        out+="{\"index\":"+std::to_string(i)+",\"type\":"+std::to_string(p[0x28])+",\"position\":"+VectorJson(position)+",\"direction\":"+VectorJson(direction)+",\"radius\":"+std::to_string(radius)+",\"colour\":"+VectorJson(colour)+"}";
    }
    return out+"]}";
}
void Status(uint8_t *player) {
    char fields[4096];
    CVector p{}; float health=0, armour=0; int state=-1, inVehicle=0, wanted=-1;
    if (player) {
        p=game::EntityPos(player); health=game::Health(player); armour=game::Armour(player);
        state=game::PedState(player); inVehicle=game::PedInVehicle(player);
        uint8_t *w=*reinterpret_cast<uint8_t **>(player+0x778); if (w) wanted=*reinterpret_cast<int32_t *>(w+0x20);
    }
    const int weaponSlot=player?game::PedCurrentWeaponSlot(player):-1;
    const uint8_t *held=player&&weaponSlot>=0&&weaponSlot<game::kWeaponSlots?game::PedWeapon(player,weaponSlot):nullptr;
    uint8_t *npc = player ? Owned() : nullptr; CVector np{};
    if (npc) np=game::EntityPos(npc);
    std::string heli="[";
    if (player) for(int i=0;i<4;++i) {
        if (i) heli+=",";
        uint8_t *v=g.helis[i]; bool valid=v && g.vehicle(g.vehicleRef(v))==v;
        if (!valid) { heli+="null"; continue; }
        char h[512]; const CVector hp=game::EntityPos(v);
        snprintf(h,sizeof h,"{\"slot\":%d,\"ref\":%d,\"model\":%d,\"state\":%u,\"light\":%.5f,\"damage\":%d,\"position\":[%.4f,%.4f,%.4f]}",
                 i,g.vehicleRef(v),int(game::EntityModel(v)),unsigned(v[0x390]),double(*reinterpret_cast<float *>(v+0x3ac)),
                 *reinterpret_cast<int32_t *>(v+0x3f4),double(hp.x),double(hp.y),double(hp.z)); heli+=std::string(h,strlen(h)-1)+HeliDetails(i,v)+"}";
    }
    heli+="]";
    snprintf(fields,sizeof fields,
        ",\"pid\":%d,\"main_thread\":%s,\"symbols\":%s,\"hud_hook\":%s,\"hud_calls\":%llu,\"ready\":%s,\"area\":%d,\"game_ms\":%u,\"frame\":%u,"
        "\"clock\":[%u,%u,%u],\"freeze\":%u,\"slow_mode\":%u,\"time_scale\":%.5f,\"weather\":{\"old\":%d,\"new\":%d,\"forced\":%d,\"interpolation\":%.6f,\"rain\":%.6f,\"clouds\":%.6f},"
        "\"rt\":%d,\"rt_env_present\":%s,\"grade\":%d,\"player\":{\"position\":[%.4f,%.4f,%.4f],\"health\":%.5f,\"armour\":%.5f,\"state\":%d,\"in_vehicle\":%d,\"wanted\":%d,\"weapon_slot\":%d,\"weapon_type\":%d,\"weapon_ammo\":%d},"
        "\"npc\":{\"ref\":%d,\"present\":%s,\"health\":%.5f,\"state\":%d,\"position\":[%.4f,%.4f,%.4f]},\"reactions\":%d,\"random_helis\":%d,\"capture\":%s,\"capture_samples\":%zu,\"capture_full\":%s",
        getpid(),pthread_main_np()?"true":"false",gSymbols?"true":"false",gNextHud?"true":"false",(unsigned long long)gHudCalls,player?"true":"false",
        g.area?*g.area:-1,g.time?*g.time:0,g.frame?*g.frame:0,
        g.hour?unsigned(*g.hour):0,g.minute?unsigned(*g.minute):0,g.seconds?unsigned(*g.seconds):0,g.freeze?unsigned(*g.freeze):0,g.slow?unsigned(*g.slow):0,g.timeScale?double(*g.timeScale):0,
        g.weatherOld?int(*g.weatherOld):-1,g.weatherNew?int(*g.weatherNew):-1,g.weatherForced?int(*g.weatherForced):-1,
        g.weatherInterpolation?double(*g.weatherInterpolation):0,g.rain?double(*g.rain):0,g.clouds?double(*g.clouds):0,
        g.menu?int(g.menu[0x72a]):-1,getenv("RW_MTL_RT")?"true":"false",gGrade?int(*gGrade):-1,
        double(p.x),double(p.y),double(p.z),double(health),double(armour),state,inVehicle,wanted,weaponSlot,held?game::WeaponType(held):-1,held?game::WeaponAmmoTotal(held):0,
        gOwned,npc?"true":"false",npc?double(game::Health(npc)):0,npc?game::PedState(npc):-1,double(np.x),double(np.y),double(np.z),
        player?ReactionCount():-1,player?int(*g.heliCount):-1,gCapture?"true":"false",gSampleCount,gSamplesFull?"true":"false");
    char lighting[1280]; CVector sun{};
    if (player && g.sunIndex && *g.sunIndex>=0 && *g.sunIndex<16) sun=g.sunVectors[*g.sunIndex];
    snprintf(lighting,sizeof lighting,",\"foreground\":%s,\"menu_active\":%s,\"user_pause\":%s,\"code_pause\":%s,\"lighting\":{\"rt_enabled\":%s,\"rt_active\":%s,\"sun_vector\":[%.6f,%.6f,%.6f],\"sun_screen\":[%.4f,%.4f],\"shadow_strength\":%d,\"ambient\":[%.6f,%.6f,%.6f],\"directional\":[%.6f,%.6f,%.6f]}",
             player&&g.foreground()?"true":"false",g.menu&&g.menu[0x38]?"true":"false",g.userPause&&*g.userPause?"true":"false",g.codePause&&*g.codePause?"true":"false",
             player&&g.rtEnabled()?"true":"false",player&&g.rtActive()?"true":"false",double(sun.x),double(sun.y),double(sun.z),
             g.sunX?double(*g.sunX):0,g.sunY?double(*g.sunY):0,g.shadowStrength?int(*g.shadowStrength):0,
             g.ambient[0]?double(*g.ambient[0]):0,g.ambient[1]?double(*g.ambient[1]):0,g.ambient[2]?double(*g.ambient[2]):0,
             g.directional[0]?double(*g.directional[0]):0,g.directional[1]?double(*g.directional[1]):0,g.directional[2]?double(*g.directional[2]):0);
    const uint8_t *m4=player?g.weaponInfo(26):nullptr;const int m4model=m4?*reinterpret_cast<const int32_t *>(m4+0x54):-1;
    std::string loaded="[";for(int model:{165,1,2,m4model}) { if(loaded.size()>1)loaded+=",";loaded+="{\"model\":"+std::to_string(model)+",\"state\":"+std::to_string(g.stream&&model>=0&&model<6500?int(g.stream[model*0x20+0x10]):-1)+"}"; }loaded+="]";
    char pipes[512];snprintf(pipes,sizeof pipes,",\"grade_can_enable\":%s,\"clouds\":%d,\"custom_pipes\":{\"lightmap\":[%d,%.6f],\"gloss\":[%d,%.6f],\"rimlight\":[%d,%.6f]}",gGradeInitiallyReady?"true":"false",gClouds?int(*gClouds):-1,g.pipeEnable[0]?int(*g.pipeEnable[0]):-1,g.pipeMult[0]?double(*g.pipeMult[0]):0,g.pipeEnable[1]?int(*g.pipeEnable[1]):-1,g.pipeMult[1]?double(*g.pipeMult[1]):0,g.pipeEnable[2]?int(*g.pipeEnable[2]):-1,g.pipeMult[2]?double(*g.pipeMult[2]):0);
    char filter[512];snprintf(filter,sizeof filter,",\"native_filter\":{\"mode\":%d,\"intensity\":%.6f,\"blur_rgb\":[%.6f,%.6f,%.6f],\"extra_colour\":{\"enabled\":%d,\"index\":%d,\"interpolation\":%.6f},\"camera_blur\":[%d,%d]},\"hidden_globals_enabled\":%s",g.filterMode?*g.filterMode:-1,g.filterIntensity?double(*g.filterIntensity):0,g.blur[0]?double(*g.blur[0]):0,g.blur[1]?double(*g.blur[1]):0,g.blur[2]?double(*g.blur[2]):0,g.extraColourOn?*g.extraColourOn:-1,g.extraColour?*g.extraColour:-1,g.extraColourInterpolation?double(*g.extraColourInterpolation):0,g.camera?*reinterpret_cast<int32_t *>(g.camera+0xb8):-1,g.camera?*reinterpret_cast<int32_t *>(g.camera+0xc8):-1,gHiddenMatched?"true":"false");
    std::string cameraState="null";
    if(player&&g.camera&&g.camera[0x7e]<3){
        const uint8_t *cam=g.camera+0x190+size_t(g.camera[0x7e])*0x1e0;
        const CVector eye=*reinterpret_cast<const CVector *>(g.camera+0x30);
        CVector head{};g.componentPosition(player+0x2c0,&head,2);
        cameraState="{\"active_index\":"+std::to_string(g.camera[0x7e])+",\"mode\":"+std::to_string(*reinterpret_cast<const uint16_t *>(cam+0xc))+
            ",\"car_zoom\":"+std::to_string(*reinterpret_cast<const int32_t *>(g.camera+0xe4))+
            ",\"looking_behind\":"+std::to_string(cam[7])+",\"looking_at_player\":"+std::to_string(g.camera[0x63])+",\"widescreen\":"+std::to_string(g.camera[0x75])+
            ",\"position\":"+VectorJson(eye)+",\"front\":"+VectorJson(*reinterpret_cast<const CVector *>(g.camera+0x10))+
            ",\"source\":"+VectorJson(*reinterpret_cast<const CVector *>(cam+0x174))+
            ",\"cam_front\":"+VectorJson(*reinterpret_cast<const CVector *>(cam+0x168))+
            ",\"cam_up\":"+VectorJson(*reinterpret_cast<const CVector *>(cam+0x18c))+
            ",\"ped_head\":"+VectorJson(head);
        uint8_t *vehicle=game::PedInVehicle(player)?game::PedVehicle(player):nullptr;
        const int ref=vehicle?g.vehicleRef(vehicle):-1;
        if(vehicle&&ref>=0&&g.vehicle(ref)==vehicle){
            auto local=[&](CVector world){const CVector origin=game::EntityPos(vehicle);world={world.x-origin.x,world.y-origin.y,world.z-origin.z};
                const auto r=*reinterpret_cast<const CVector *>(vehicle+8),f=*reinterpret_cast<const CVector *>(vehicle+0x18),u=*reinterpret_cast<const CVector *>(vehicle+0x28);
                return CVector{r.x*world.x+r.y*world.y+r.z*world.z,f.x*world.x+f.y*world.y+f.z*world.z,u.x*world.x+u.y*world.y+u.z*world.z};};
            cameraState+=",\"eye_local\":"+VectorJson(local(eye))+",\"head_local\":"+VectorJson(local(head));
            if(gCameraCockpit)cameraState+=",\"plugin_cockpit_matches_vehicle\":"+std::to_string(*gCameraCockpit==vehicle);
            if(gCameraHeadAbout)cameraState+=",\"plugin_head_about_matches_vehicle\":"+std::to_string(*gCameraHeadAbout==vehicle);
        }
        if(gFirstPerson)cameraState+=",\"first_person_enabled\":"+std::to_string(*gFirstPerson);
        if(gCameraFirstPerson)cameraState+=",\"plugin_first_person\":"+std::to_string(*gCameraFirstPerson);
        if(gCameraHeadReady)cameraState+=",\"plugin_head_ready\":"+std::to_string(*gCameraHeadReady);
        if(gCameraLookPitch&&std::isfinite(*gCameraLookPitch))cameraState+=",\"plugin_look_pitch\":"+std::to_string(*gCameraLookPitch);
        if(gCameraLookYaw&&std::isfinite(*gCameraLookYaw))cameraState+=",\"plugin_look_yaw\":"+std::to_string(*gCameraLookYaw);
        if(gCameraHeadX&&gCameraHeadY&&gCameraHeadZ)cameraState+=",\"plugin_head_local\":"+VectorJson(CVector{*gCameraHeadX,*gCameraHeadY,*gCameraHeadZ});
        if(gCameraCockpit)cameraState+=",\"plugin_cockpit_present\":"+std::to_string(*gCameraCockpit!=nullptr);
        if(gCameraHeadAbout)cameraState+=",\"plugin_head_about_present\":"+std::to_string(*gCameraHeadAbout!=nullptr);
        cameraState+="}";
    }
    std::string carState="null";
    if(player && game::PedInVehicle(player)) {
        uint8_t *car=game::PedVehicle(player);const int ref=car?g.vehicleRef(car):-1;
        const bool valid=car&&ref>=0&&g.vehicle(ref)==car;
        if(valid){
            char text[512];const int kind=*reinterpret_cast<const int32_t *>(car+0x348);
            snprintf(text,sizeof text,"{\"pointer\":\"%p\",\"ref\":%d,\"generation_valid\":true,\"player_driver\":%s,\"model\":%d,\"vehicle_class\":%d,\"status\":%d,\"created_by\":%d,\"autopilot_mission\":%u,\"mission_active\":%s,\"cutscene_running\":%s,\"physical_flags_156\":%u,\"vehicle_flags_295\":%u,\"damage_flags_297\":%u",
                static_cast<void *>(car),ref,*reinterpret_cast<uint8_t **>(car+0x218)==player?"true":"false",game::EntityModel(car),kind,(car[0x60]>>3),int(car[0x294]),unsigned(car[0x19e]),g.onMission&&g.onMission()?"true":"false",g.cutscene&&*g.cutscene?"true":"false",unsigned(car[0x156]),unsigned(car[0x295]),unsigned(car[0x297]));
            carState=text;
            carState+=",\"entity_flags\":"+std::to_string(game::EntityFlags(car))+",\"entity_flags_64\":"+std::to_string(*reinterpret_cast<const uint32_t *>(car+0x64))+
                ",\"collision_piece\":"+std::to_string(*reinterpret_cast<const uint16_t *>(car+0x154))+
                ",\"collision_normal\":"+VectorJson(*reinterpret_cast<const CVector *>(car+0x148));
            auto scalar=[&](const char *name,size_t offset){const float value=*reinterpret_cast<const float *>(car+offset);carState+=",\""+std::string(name)+"\":"+(std::isfinite(value)?std::to_string(value):"null");};
            scalar("health",0x2a0);scalar("mass",0xcc);scalar("turn_mass",0xd0);scalar("force_multiplier",0xd4);scalar("air_resistance",0xd8);scalar("collision_damage",0x13c);
            const CVector speed=game::EntitySpeed(car),turn=*reinterpret_cast<const CVector *>(car+0x90);
            carState+=",\"position\":"+VectorJson(game::EntityPos(car))+",\"velocity_mps\":"+VectorJson(CVector{speed.x*50,speed.y*50,speed.z*50})+
                ",\"angular_rps\":"+VectorJson(CVector{turn.x*50,turn.y*50,turn.z*50})+",\"up\":"+VectorJson(*reinterpret_cast<const CVector *>(car+0x28))+
                ",\"right\":"+VectorJson(*reinterpret_cast<const CVector *>(car+8))+",\"forward\":"+VectorJson(*reinterpret_cast<const CVector *>(car+0x18))+
                ",\"centre_of_mass\":"+VectorJson(*reinterpret_cast<const CVector *>(car+0xe4));
            // Native CPlane/CHeli allocations end before automobile rotor data.
            // A class0 full helicopter alone authorizes this farther read.
            const int model=game::EntityModel(car);
            if(kind==0&&(model==155||model==177||model==199||model==217||model==218||model==227))scalar("rotor_speed",0x5f0);
            carState+=",\"wheels\":"+WheelsJson(car);
            if(gJetEnginePower){const float power=gJetEnginePower(car);carState+=",\"engine_power\":"+(std::isfinite(power)?std::to_string(power):"null");}
            if(gJetGearDown&&gAirportModel&&model==*gAirportModel){
                const float deployment=gJetGearDown(car);
                carState+=",\"jet_gear_down\":"+(std::isfinite(deployment)?std::to_string(deployment):"null");
            }
            carState+="}";
        }
    }
    const CVector velocity=player?game::EntitySpeed(player):CVector{};
    std::string padState="null";
    if(player&&g.pad)if(const uint8_t *pad=g.pad(0)){
        // Read the native merged CControllerState, never keyboard text/events.
        padState="[";for(int i=0;i<21;++i){if(i)padState+=",";padState+=std::to_string(*reinterpret_cast<const int16_t *>(pad+2*i));}padState+="]";
    }
    const uint8_t *currentPad=player&&g.pad?g.pad(0):nullptr;
    const std::string motion="{\"move_state\":"+std::to_string(player?game::PedMoveState(player):-1)+",\"velocity_mps\":"+VectorJson(CVector{velocity.x*50,velocity.y*50,velocity.z*50})+",\"pad_new\":"+padState+
        ",\"pad_mode\":"+std::to_string(currentPad?*reinterpret_cast<const uint16_t *>(currentPad+0xec):-1)+",\"pad_affected_by_controller\":"+std::to_string(g.padAffected?int(*g.padAffected):-1)+",\"sources\":"+InputSourcesJson(currentPad)+"}";
    std::string flight="{\"mission_active\":"+std::string(g.onMission&&g.onMission()?"true":"false")+",\"cutscene_running\":"+std::string(g.cutscene&&*g.cutscene?"true":"false")+
        ",\"planes_enabled\":"+(gPlanesEnabled?std::to_string(*gPlanesEnabled):"null")+",\"forces_ready\":"+(gPlaneForcesReady?std::to_string(*gPlaneForcesReady):"null")+
        ",\"roster_ready\":"+(gPlaneRosterReady?std::to_string(*gPlaneRosterReady):"null")+",\"roster_attempt_ms\":"+(gPlaneAttempt?std::to_string(*gPlaneAttempt):"null")+
        ",\"roster_previous_ms\":"+(gPlanePrevious?std::to_string(*gPlanePrevious):"null")+",\"dodo_stream_state\":"+std::to_string(g.stream?int(g.stream[181*0x20+0x10]):-1);
    const uint8_t *dodoModel=player&&g.models?g.models[181]:nullptr;
    const uint8_t *dodoCol=dodoModel?*reinterpret_cast<uint8_t *const *>(dodoModel+0x20):nullptr;
    if(dodoCol){flight+=",\"dodo_col_min\":"+VectorJson(*reinterpret_cast<const CVector *>(dodoCol+0x10))+
        ",\"dodo_col_spheres\":"+std::to_string(*reinterpret_cast<const int16_t *>(dodoCol+0x28));}
    flight+=",\"airport_jets\":{\"ready\":"+(gAirportReady?std::to_string(*gAirportReady):"null")+
        ",\"shape_ready\":"+(gAirportShapeReady?std::to_string(*gAirportShapeReady):"null")+
        ",\"model\":"+(gAirportModel?std::to_string(*gAirportModel):"null");
    const int airportModel=gAirportModel?*gAirportModel:-1;
    const uint8_t *airportInfo=airportModel>=240&&airportModel<=249&&g.models?g.models[airportModel]:nullptr;
    if(airportInfo){
        flight+=",\"model_kind\":"+std::to_string(airportInfo[0x1d])+
            ",\"model_refs\":"+std::to_string(*reinterpret_cast<const uint16_t *>(airportInfo+0x2c))+
            ",\"stream_state\":"+std::to_string(g.stream?int(g.stream[airportModel*32+0x10]):-1);
        const uint8_t *col=*reinterpret_cast<uint8_t *const *>(airportInfo+0x20);
        if(col)flight+=",\"col_min\":"+VectorJson(*reinterpret_cast<const CVector *>(col+0x10))+
            ",\"col_spheres\":"+std::to_string(*reinterpret_cast<const int16_t *>(col+0x28));
    }
    flight+="}";
    flight+=",\"tower_models\":[";
    for(int id:{1868,1882,1924,1857}){
        if(id!=1868)flight+=",";
        const uint8_t *info=player&&g.models?g.models[id]:nullptr;
        flight+="{\"model\":"+std::to_string(id)+",\"stream_state\":"+std::to_string(g.stream?int(g.stream[id*32+0x10]):-1);
        if(info&&info[0x1d]==1){
            const uint8_t *col=*reinterpret_cast<uint8_t *const *>(info+0x20);
            flight+=",\"original_atomic_ready\":"+std::string(*reinterpret_cast<void *const *>(info+0x30)?"true":"false")+
                ",\"references\":"+std::to_string(*reinterpret_cast<const uint16_t *>(info+0x2c));
            if(col)flight+=",\"native_col_spheres\":"+std::to_string(*reinterpret_cast<const int16_t *>(col+0x28))+
                ",\"native_col_boxes\":"+std::to_string(*reinterpret_cast<const int16_t *>(col+0x2a))+
                ",\"native_col_triangles\":"+std::to_string(*reinterpret_cast<const int16_t *>(col+0x2c));
        }
        flight+="}";
    }
    flight+="],\"aircraft\":[";int aircraft=0;
    const uint8_t *pool=player&&g.vehiclePool?*g.vehiclePool:nullptr;
    const uint8_t *flags=pool?*reinterpret_cast<const uint8_t *const *>(pool+8):nullptr;
    const int capacity=pool?*reinterpret_cast<const int32_t *>(pool+0x10):0;
    if(flags&&capacity>0&&capacity<100000){
      for(int i=0;i<capacity&&aircraft<32;++i){
        if(flags[i]&0x80)continue;const int ref=i*256|flags[i];const uint8_t *vehicle=g.vehicle(ref);if(!vehicle)continue;
        const int model=game::EntityModel(vehicle);
        const int kind=*reinterpret_cast<const int32_t *>(vehicle+0x348);
        // Include registered native CPlane actors without guessing a new model
        // ID. All reads here stay inside the common CVehicle allocation.
        if(kind!=4&&model!=155&&model!=165&&model!=177&&model!=190&&model!=199&&model!=217&&model!=218&&model!=227)continue;
        if(aircraft++)flight+=",";
        flight+="{\"ref\":"+std::to_string(ref)+",\"model\":"+std::to_string(model)+",\"class\":"+std::to_string(kind)+
            ",\"created_by\":"+std::to_string(vehicle[0x294])+",\"position\":"+VectorJson(game::EntityPos(vehicle));
        const float health=game::VehicleHealth(vehicle);
        const CVector speed=*reinterpret_cast<const CVector *>(vehicle+0x84),turn=*reinterpret_cast<const CVector *>(vehicle+0x90);
        flight+=",\"health\":"+(std::isfinite(health)?std::to_string(health):"null")+
            ",\"velocity_mps\":"+VectorJson(CVector{speed.x*50,speed.y*50,speed.z*50})+
            ",\"angular_rps\":"+VectorJson(CVector{turn.x*50,turn.y*50,turn.z*50})+
            ",\"right\":"+VectorJson(*reinterpret_cast<const CVector *>(vehicle+8))+
            ",\"forward\":"+VectorJson(*reinterpret_cast<const CVector *>(vehicle+0x18))+
            ",\"up\":"+VectorJson(*reinterpret_cast<const CVector *>(vehicle+0x28))+
            ",\"wheels\":"+WheelsJson(vehicle)+"}";
      }
    }
    flight+="]}";
    std::string out="{\"last_id\":"+std::to_string(gLastId)+",\"command\":"+Quote(gLastCommand)+",\"result\":"+Quote(gResult)+fields+lighting+pipes+filter+",\"label\":"+Quote(gLabel)+",\"helis\":"+heli+",\"apb\":"+ApbJson(player)+",\"npc_pose\":"+NpcPoseJson(npc)+",\"crew_models\":"+loaded+",\"window\":"+WindowJson()+",\"player_bones\":"+PlayerBonesJson(player)+",\"exhaust_particles\":"+(player?ExhaustParticlesJson():"null")+",\"camera_debug\":"+cameraState+",\"player_car\":"+carState+",\"player_motion\":"+motion+",\"point_lights\":"+PointLightsJson()+",\"flight\":"+flight+"}\n";
    if (!AtomicWrite(gStatus,out)) Log("status write failed: %s",strerror(errno));
}
void DumpFrames() {
    std::string out="label,monotonic_ns,interval_ns,hud_chain_ns,game_frame,game_ms,reactions,random_helis,valid,flags,heli0_ref,heli0_state,heli0_crew,heli1_ref,heli1_state,heli1_crew,loaded_mask,npc_ref,npc_state,npc_mode,npc_weight,npc_balance,npc_root_owner,npc_root_error,npc_pelvis_offset_error,root_x,root_y,root_z,pelvis_x,pelvis_y,pelvis_z,head_x,head_y,head_z,foot_l_x,foot_l_y,foot_l_z,foot_r_x,foot_r_y,foot_r_z\n";
    out.reserve(gSampleCount*400);
    char row[1024];
    for (size_t i=0;i<gSampleCount;++i) {
        const Sample &s=gSamples[i];
        char rootError[40];if(s.physicalRoot)snprintf(rootError,sizeof rootError,"%.6f",double(s.rootError));else std::strcpy(rootError,"null");
        snprintf(row,sizeof row,"%s,%llu,%llu,%llu,%u,%u,%d,%d,%u,%u,%d,%d,%d,%d,%d,%d,%u,%d,%d,%d,%.6f,%.6f,%s,%s,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",gLabel.c_str(),(unsigned long long)s.ns,(unsigned long long)s.interval,(unsigned long long)s.hud,s.frame,s.gameMs,s.reactions,s.helis,unsigned(s.flags==63),s.flags,s.heliRef[0],s.heliState[0],s.crewCount[0],s.heliRef[1],s.heliState[1],s.crewCount[1],s.loadedMask,s.npcRef,s.npcState,s.npcMode,double(s.weight),double(s.balance),s.physicalRoot?"physical":"native",rootError,double(s.pelvisOffsetError),double(s.root.x),double(s.root.y),double(s.root.z),double(s.pelvis.x),double(s.pelvis.y),double(s.pelvis.z),double(s.head.x),double(s.head.y),double(s.head.z),double(s.footL.x),double(s.footL.y),double(s.footL.z),double(s.footR.x),double(s.footR.y),double(s.footR.z)); out+=row;
    }
    gResult=AtomicWrite(gFrames,out)?"frame capture written: "+std::to_string(gSampleCount)+" samples":"frame capture write failed";
}
bool WeaponModelReady(int type) {
    if(type<0 || type>36)return false;
    const uint8_t *info=g.weaponInfo(type);if(!info)return false;
    const int model=*reinterpret_cast<const int32_t *>(info+0x54);
    if(model<0)return true;
    if(model>=6500)return false;
    if(g.stream[model*0x20+0x10]==1)return true;
    g.request(model,0);return false;
}
void Execute(const probe::Command &c, uint8_t *player) {
    const auto &a=c.args;
    auto num=[&](size_t i) { return std::strtod(a[i].c_str(),nullptr); };
    auto vec=[&](size_t i) { return CVector{float(num(i)),float(num(i+1)),float(num(i+2))}; };
    gResult="ok";
    if(c.name=="status" || c.name=="npcpose") return;
    if(c.name=="window") {
        if(!gInHud || !pthread_main_np()) { gResult="window commands require the main-thread HUD boundary";return; }
        if(a[0]=="status")gResult=WindowJson();
        else {
            probe::NativeWindow(num(1)!=0,CurrentCocoaWindow(),g.menu?*reinterpret_cast<const int32_t *>(g.menu+0x718):-1,
                                Now(),NativeWindowStyle,ToggleNativeWindow,gWindowTransition,gResult);
            gResult+="; "+WindowJson();
        }
        return;
    }
    if(!gInHud || !player) { gResult="not ready; native commands require initialized gameplay at HUD boundary"; return; }
    if(c.name=="frames") {
        if(a[0]=="start") { gCapture=true;gSamplesFull=false;gSampleCount=0;gLastFrameNs=0;gLabel=a[1];gResult="frame capture started"; }
        else { gCapture=false;DumpFrames(); } return;
    }
    Originals(player);
    if(c.name=="restore") {
        if(gCameraOwned) { g.cameraRestore(g.camera);gCameraOwned=false; }
        g.clock(gOriginalHour,gOriginalMinute);*g.freeze=gOriginalFreeze;
        *g.weatherOld=gOriginalWeatherOld;*g.weatherNew=gOriginalWeatherNew;*g.weatherForced=gOriginalWeatherForced;
        g.menu[0x72a]=gOriginalRt;if(gGrade)*gGrade=gGradeInitiallyReady?gOriginalGrade:0;
        if(gClouds)*gClouds=gOriginalClouds;for(int i=0;i<3;++i) { *g.pipeEnable[i]=gOriginalPipeEnable[i];*g.pipeMult[i]=gOriginalPipeMult[i]; }
        *g.filterMode=gOriginalFilterMode;*g.filterIntensity=gOriginalFilterIntensity;
        if(uint8_t *ped=Owned();ped && Eligible(ped)) { g.removePed(ped);ForgetOwned(); }
        std::string weaponResult;
        if(gWeaponSnapshot.ped==player && g.ped(gWeaponSnapshot.ref)==player && !WeaponModelReady(gWeaponSnapshot.type))
            weaponResult="original weapon model requested; retry restore after streaming";
        else if(probe::RestoreWeapon(player,g.ped,g.setWeaponSlot,gWeaponSnapshot,weaponResult))weaponResult="restored original owned weapon slot";
        gResult="restored captured runtime clock/weather/RT/grade/clouds/custom pipes/native filter, native camera, removed owned eligible NPC; "+weaponResult;return;
    }
    if(c.name=="clock") { g.clock(uint8_t(num(0)),uint8_t(num(1)));*g.freeze=uint8_t(num(2));return; }
    if(c.name=="weather") { if(num(0)<0)g.weatherRelease();else g.weather(int16_t(num(0)));return; }
    if(c.name=="rt") { if(getenv("RW_MTL_RT")) { gResult="refused: RW_MTL_RT is present; restart with env -u RW_MTL_RT";return; } g.menu[0x72a]=uint8_t(num(0));return; }
    if(c.name=="grade") { probe::SetGradeReady(gGrade,gGradeInitiallyReady,uint8_t(num(0)),gResult);return; }
    if(c.name=="clouds") { if(!gClouds)gResult="unavailable: build plugin UUID does not match generated config offset";else *gClouds=uint8_t(num(0));return; }
    if(c.name=="lightmap" || c.name=="gloss" || c.name=="rimlight") {
        const int i=c.name=="lightmap"?0:c.name=="gloss"?1:2;*g.pipeEnable[i]=uint8_t(num(0));if(a.size()==2)*g.pipeMult[i]=float(num(1));return;
    }
    if(c.name=="filter") { *g.filterMode=int32_t(num(0));if(a.size()==2)*g.filterIntensity=float(num(1));return; }
    if(c.name=="camera") {
        if(a.size()==1) { g.cameraRestore(g.camera);gCameraOwned=false; }
        else { const CVector from=vec(0),to=vec(3),up{};g.cameraPosition(g.camera,from,up);g.cameraTarget(g.camera,to,2,1);gCameraOwned=true; }
        return;
    }
    // A bounded move inside the already loaded estate lets the normal native
    // doorway perform its own area transition. Never change currArea or stream
    // another interior through this diagnostic exception.
    bool estateExit=false;
    if(c.name=="teleport"&&*g.area==2&&!game::PedInVehicle(player)&&!g.onMission()){
        const CVector from=game::EntityPos(player),to=vec(0);
        estateExit=from.x>=-430&&from.x<=-330&&from.y>=-625&&from.y<=-530&&from.z>=15&&from.z<=40&&
            to.x>=-380&&to.x<=-376&&to.y>=-546&&to.y<=-540&&to.z>=16&&to.z<=19;
    }
    if(!WorldAllowed(player,estateExit))return;
    if(c.name=="playerhit" || c.name=="carroll") {
        if(!pthread_main_np() || !g.foreground() || !probe::PlayerDiagnosticContext(g.camera,g.pad(0),g.onMission()) || ReadyPlayer()!=player) {
            gResult="refused: requires foreground player-camera gameplay, controls enabled, no mission/script";return;
        }
        const int ref=g.pedRef(player);
        if(c.name=="playerhit") {
            probe::PlayerHitResult hit;
            if(probe::HitPlayer(player,ref,float(num(0)),g.player,g.ped,g.damage,hit,gResult)) {
                char text[256];snprintf(text,sizeof text,"native_result=%s player_ref=%d health=%.5f->%.5f armour=%.5f->%.5f",hit.native?"true":"false",ref,double(hit.healthBefore),double(hit.healthAfter),double(hit.armourBefore),double(hit.armourAfter));gResult=text;
            }
        } else {
            uint8_t *car=game::PedInVehicle(player)?game::PedVehicle(player):nullptr;
            const int carRef=car?g.vehicleRef(car):-1;
            if(probe::RollPlayerCar(player,ref,car,carRef,num(0)!=0,g.player,g.ped,g.vehicle,g.updateMatrix,g.updateFrame,gResult)) {
                if(g.vehicle(carRef)==car){char text[128];const auto &up=*reinterpret_cast<const CVector *>(car+0x28);snprintf(text,sizeof text,"; car_ref=%d up=[%.5f,%.5f,%.5f]",carRef,double(up.x),double(up.y),double(up.z));gResult+=text;}
            }
        }
        return;
    }

    if(c.name=="weapon") {
        if(game::PedInVehicle(player) || game::Health(player)<=0) { gResult="refused: select weapons only for the living on-foot player";return; }
        const int type=int(num(0));
        if(probe::OwnedWeaponSlot(player,type)<0) { gResult="refused: requested weapon is not already owned";return; }
        if(!WeaponModelReady(type)) { gResult="owned weapon model requested; retry weapon after streaming";return; }
        if(probe::SelectOwnedWeapon(player,g.pedRef(player),type,g.ped,g.setWeaponSlot,gWeaponSnapshot,gResult))
            gResult="selected already-owned weapon type="+std::to_string(type)+" slot="+std::to_string(game::PedCurrentWeaponSlot(player));
        return;
    }
    if(c.name=="npcshot") {
        uint8_t *ped=Owned();if(!Eligible(ped)) { gResult="owned NPC absent or no longer eligible";return; }
        if(game::PedInVehicle(player) || game::Health(player)<=0) { gResult="refused: NPC shot fixture requires the living on-foot player";return; }
        int tag=-1;for(const auto &joint:probe::kJoints)if(a[0]==joint.name)tag=joint.tag;
        uint8_t *clump=game::EntityClump(ped),*hier=clump?g.hierarchy(clump):nullptr;
        float *matrices=hier?g.matrices(hier):nullptr;const int index=hier&&tag>=0?g.boneIndex(hier,tag):-1;
        if(!matrices || index<0 || index>=128) { gResult="refused: selected owned NPC bone is unavailable";return; }
        const CVector target=*reinterpret_cast<const CVector *>(matrices+size_t(index)*16+12);
        const float before=game::Health(ped);const int ref=gOwned;probe::ShotResult shot;
        if(!probe::FireNpcShot(player,g.pedRef(player),ped,ref,vec(1),target,g.ped,g.line,g.bulletImpact,shot,gResult))return;
        ped=Owned();char result[384];snprintf(result,sizeof result,"native hooked NPC bullet weapon=%d ref=%d bone=%s native_piece=%d point=[%.6f,%.6f,%.6f] health_before=%.5f health_after=%.5f",shot.weapon,ref,a[0].c_str(),shot.piece,double(shot.point.x),double(shot.point.y),double(shot.point.z),double(before),ped?double(game::Health(ped)):0);gResult=result;return;
    }
    if(c.name=="pilotdamage" || c.name=="helishot") {
        const int slot=int(num(0));uint8_t *heli=RandomHeli(slot);
        if(!heli) { gResult="refused: selected slot is not a generation-validated random police helicopter";return; }
        if(c.name=="pilotdamage") {
            uint8_t *pilot=PilotOf(slot,heli);if(!pilot) { gResult="refused: no validated attached civilian police-model pilot in selected random helicopter";return; }
            const int ref=g.pedRef(pilot);const float before=game::Health(pilot);
            const bool result=g.damage(pilot,player,int(num(1)),float(num(2)),int(num(3)),uint8_t(num(4)));
            pilot=g.ped(ref);char text[256];snprintf(text,sizeof text,"native_result=%s pilot_ref=%d health_before=%.5f health_after=%.5f",result?"true":"false",ref,double(before),pilot?double(game::Health(pilot)):0);gResult=text;return;
        }
        const int weaponSlot=game::PedCurrentWeaponSlot(player);if(weaponSlot<0||weaponSlot>=game::kWeaponSlots) { gResult="invalid current player weapon slot";return; }
        uint8_t *weapon=game::PedWeapon(player,weaponSlot);const int type=game::WeaponType(weapon);
        if(!((type>=17&&type<=29)||type==32||type==33)) { gResult="refused: hold a real firearm for native bullet-impact fixture";return; }
        CVector localFrom{},localTo{};
        if(a[1]=="lamp") { const Wear *wear=WearOf(slot,heli);if(!wear||wear->lampOut) { gResult="lamp state unavailable or lamp already out";return; }localFrom={6,25,-30};localTo={wear->lamp.middle.x,wear->lamp.middle.y,wear->lamp.middle.z}; }
        else if(a[1]=="tail") { localFrom={-20,-6.76f,1.1f};localTo={-.15f,-6.76f,1.1f}; }
        else if(a[1]=="mast") { localFrom={30,0,2};localTo={0,0,2}; }
        else if(a[1]=="hull") { localFrom={20,-.5f,.35f};localTo={0,-.5f,.35f}; }
        else { if(!PilotOf(slot,heli)) { gResult="no validated attached pilot for cockpit ray";return; }localFrom={.42f,30,.4f};localTo={.42f,1.5f,.35f}; }
        CVector from=WorldPoint(heli,localFrom),to=WorldPoint(heli,localTo);const float length=Distance(from,to);
        if(length<.01f) { gResult="degenerate bullet-impact ray";return; }
        to={to.x+(to.x-from.x)*10/length,to.y+(to.y-from.y)*10/length,to.z+(to.z-from.z)*10/length};
        alignas(16) uint8_t point[0x30]{};*reinterpret_cast<CVector *>(point)=to;
        const int ref=g.vehicleRef(heli),before=*reinterpret_cast<int32_t *>(heli+0x3f4);
        g.bulletImpact(weapon,player,nullptr,&from,&to,point,{0,0});
        heli=g.vehicle(ref);char text[256];snprintf(text,sizeof text,"native hooked bullet impact weapon=%d heli_ref=%d damage_before=%d damage_after=%d part=%s",type,ref,before,heli?*reinterpret_cast<int32_t *>(heli+0x3f4):-1,a[1].c_str());gResult=text;return;
    }
    if(c.name=="ground" || c.name=="line") {
        alignas(16) uint8_t point[0x30]{};uint8_t *entity=nullptr;
        const CVector from=vec(0);
        const bool hit=c.name=="ground"?g.vertical(from,from.z-100,point,entity,true,false,false,true,true,false,nullptr):
            g.line(from,vec(3),point,entity,true,false,false,true,true,false,false,false);
        const CVector at=*reinterpret_cast<const CVector *>(point),normal=*reinterpret_cast<const CVector *>(point+0x10);
        char result[512];snprintf(result,sizeof result,"hit=%s position=[%.6f,%.6f,%.6f] normal=[%.6f,%.6f,%.6f] entity_type=%d model=%d",hit?"true":"false",double(at.x),double(at.y),double(at.z),double(normal.x),double(normal.y),double(normal.z),hit&&entity?int(entity[0x60]&7):-1,hit&&entity?int(game::EntityModel(entity)):-1);
        gResult=result;return;
    }
    if(c.name=="wanted") { g.wanted(player,int(num(0)));return; }
    if(c.name=="npcrun") {
        uint8_t *ped=Owned();if(!Eligible(ped) || game::Health(ped)<=0 || game::PedState(ped)>=game::kPedDie) { gResult="owned living NPC absent or no longer eligible";return; }
        const CVector at=vec(0);
        if(Distance(at,game::EntityPos(ped))>50 || Distance(at,game::EntityPos(player))>150) { gResult="refused: run destination must stay within 50 metres of owned NPC and 150 metres of player";return; }
        g.objectiveAt(ped,25,at); // OBJECTIVE_RUN_TO_AREA; native navigation produces all movement/contacts.
        return;
    }
    if(c.name=="teleport") {
        if(game::PedInVehicle(player)) { gResult="refused: player must be on foot";return; }
        const int area=*g.area;const CVector at=vec(0);
        if(!estateExit){g.collision(at);g.scene(at);}
        // Scene streaming may service callbacks; revalidate before using the player.
        if(ReadyPlayer()!=player || *g.area!=area) { gResult="world changed during streaming; teleport refused";return; }
        g.teleport(player,at);return;
    }
    if(c.name=="spawn" || c.name=="npcmove") {
        CVector at=vec(0);const CVector pp=game::EntityPos(player);
        const float dx=at.x-pp.x,dy=at.y-pp.y;
        if(dx*dx+dy*dy>150*150) { gResult="refused: NPC position must be within 150 metres of player";return; }
        bool found=false;at.z=g.ground(at.x,at.y,at.z+5,&found)+1.0f;
        if(!found || !std::isfinite(at.z)) { gResult="refused: native ground query found no ground";return; }
        if(c.name=="npcmove") {
            uint8_t *ped=Owned();if(!Eligible(ped)) { gResult="owned NPC absent or no longer eligible";return; }g.teleport(ped,at);return;
        }
        if(Owned()) { gResult="refused: one owned NPC already exists (remove it first)";return; }
        if(g.stream[7*0x20+0x10]!=1) { g.request(7,0);gResult="model 7 requested; retry spawn after streaming loads it";return; }
        // Native AddPed dereferences a null CPed allocation when the pool is
        // full, so its returned pointer cannot serve as the capacity guard.
        const uint8_t *pool=g.pedPool?*g.pedPool:nullptr;
        if(!pool || !*reinterpret_cast<uint8_t *const *>(pool)) { gResult="refused: native ped pool is unavailable";return; }
        const uint8_t *flags=*reinterpret_cast<const uint8_t *const *>(pool+8);
        const int capacity=*reinterpret_cast<const int32_t *>(pool+0x10);
        if(!probe::CanSpawnPed(flags,capacity,gResult))return;
        uint8_t *ped=g.addPed(4,7,at,-1);
        if(!ped) { gResult="native AddPed returned null";return; }
        gOwned=g.pedRef(ped);gOwnedPointer=ped;
        if(gOwned<0 || g.ped(gOwned)!=ped || !Eligible(ped)) { ForgetOwned();gResult="native spawn did not produce an eligible generation-referenced civilian";return; }
        gResult="spawned civilian model 7, ref="+std::to_string(gOwned);return;
    }
    if(c.name=="remove") {
        uint8_t *ped=Owned();if(!Eligible(ped)) { gResult="owned NPC absent or no longer eligible";return; }
        g.removePed(ped);ForgetOwned();return;
    }
    if(c.name=="damage") {
        uint8_t *ped=Owned();if(!Eligible(ped)) { gResult="owned NPC absent or no longer eligible";return; }
        const float before=game::Health(ped);
        // Exported entry is patched: this calls the live production damage hook.
        const bool native=g.damage(ped,player,int(num(0)),float(num(1)),int(num(2)),uint8_t(num(3)));
        ped=Owned();const float after=ped?game::Health(ped):0;
        char result[256];snprintf(result,sizeof result,"native_result=%s health_before=%.5f health_after=%.5f delta=%.5f state=%d",native?"true":"false",double(before),double(after),double(before-after),ped?game::PedState(ped):-1);gResult=result;return;
    }
}
void Poll(uint8_t *player, bool force=false) {
    if(!pthread_main_np())return;
    const uint64_t now=Now();
    if(!force && now-gLastPollNs<50000000)return;
    gLastPollNs=now;
    const std::string claimed=gCommand+".processing."+std::to_string(getpid());
    if(rename(gCommand.c_str(),claimed.c_str())==0) {
        const int fd=open(claimed.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK);struct stat st{};std::string input;
        if(fd>=0 && fstat(fd,&st)==0 && S_ISREG(st.st_mode) && st.st_uid==getuid() && st.st_size>0 && st.st_size<=1024) {
            input.resize(size_t(st.st_size));size_t used=0;
            while(used<input.size()) { ssize_t n=read(fd,input.data()+used,input.size()-used);if(n<=0)break;used+=size_t(n); }
            input.resize(used);
        }
        if(fd>=0)close(fd);unlink(claimed.c_str());
        probe::Command command;std::string error;
        if(probe::Parse(input,command,error)) {
            gLastId=command.id;gLastCommand=command.name;Log("request=%llu command=%s",(unsigned long long)command.id,input.c_str());Execute(command,player);Log("result=%s",gResult.c_str());
        } else { gLastId=command.id;gLastCommand="invalid";gResult=error;Log("invalid command: %s",error.c_str()); }
        Status(ReadyPlayer());gLastStatusNs=now;
    } else if(!gCapture && now-gLastStatusNs>=1000000000) { Status(player);gLastStatusNs=now; }
}
void HudDraw() {
    if(!gNextHud || gInHud || !pthread_main_np())return;
    gInHud=true;
    const uint64_t start=Now();++gHudCalls;
    uint8_t *player=ReadyPlayer();gReady=player!=nullptr;
    if((gWasReady && !gReady) || (g.time && gPreviousTime && (*g.time<gPreviousTime || *g.time-gPreviousTime>10000))) {
        ForgetOwned();gCameraOwned=false;gOriginals=false;gWeaponSnapshot={};Log("world readiness/timeline changed: discarded owned generation and world snapshot; preserved process-global toggle baseline");
    }
    if(g.time)gPreviousTime=*g.time;gWasReady=gReady;
    // Native mutating commands run before the chained HUD call, outside its renderer.
    Poll(player);
    const uint64_t chainStart=Now();gNextHud();const uint64_t end=Now();
    if(gCapture) {
        const uint64_t interval=gLastFrameNs?start-gLastFrameNs:0;
        const uint32_t flags=(gReady?1u:0u)|(g.foreground()?2u:0u)|(!g.menu[0x38]?4u:0u)|(!*g.userPause&&!*g.codePause?8u:0u)|(!*g.cutscene?16u:0u)|(interval&&interval<=250000000?32u:0u);
        if(gSampleCount<kMaxSamples) {
            Sample &s=gSamples[gSampleCount++];s={};
            s.ns=start;s.interval=interval;s.hud=end-chainStart;s.frame=*g.frame;s.gameMs=*g.time;s.reactions=gReady?ReactionCount():-1;s.helis=gReady?int(*g.heliCount):-1;s.flags=flags;
            for(int i=0;i<2;++i) {
                uint8_t *heli=gReady?RandomHeli(i):nullptr;s.heliRef[i]=heli?g.vehicleRef(heli):-1;s.heliState[i]=heli?int(heli[0x390]):-1;
                const Crew *crew=CrewOf(i,heli);s.crewCount[i]=0;if(crew)for(int ref:{crew->pilot,crew->ped[0],crew->ped[1]})if(uint8_t *ped=ref>=0?g.ped(ref):nullptr;ped&&*reinterpret_cast<uint8_t **>(ped+0x720)==heli)++s.crewCount[i];
            }
            const uint8_t *m4=gReady?g.weaponInfo(26):nullptr;const int m4model=m4?*reinterpret_cast<const int32_t *>(m4+0x54):-1;
            const int models[]={165,1,2,m4model};for(int i=0;i<4;++i)if(models[i]>=0&&models[i]<6500&&g.stream[models[i]*0x20+0x10]==1)s.loadedMask|=1u<<i;
            uint8_t *npc=gReady?Owned():nullptr;s.npcRef=npc?gOwned:-1;s.npcState=npc?game::PedState(npc):-1;s.npcMode=-1;
            if(npc) { s.root=game::EntityPos(npc);if(const ReactionPerson *p=ReactionOf(npc)) {
                s.npcMode=int(p->physical.State());s.weight=p->physical.Weight();s.balance=p->physical.Balance();
                const auto &pose=p->physical.Positions();s.pelvis=pose[reactphysics::Pelvis];s.head=pose[reactphysics::Head];s.footL=pose[reactphysics::FootL];s.footR=pose[reactphysics::FootR];
                const CVector expected{s.pelvis.x-p->pelvisOffset.x,s.pelvis.y-p->pelvisOffset.y,s.pelvis.z-p->pelvisOffset.z};s.physicalRoot=probe::PhysicalRoot(p->physical.Active(),p->physical.State());s.pelvisOffsetError=Distance(expected,s.root);s.rootError=s.physicalRoot?s.pelvisOffsetError:0;
            } }
        }
        else { gCapture=false;gSamplesFull=true;DumpFrames(); }
        gLastFrameNs=start;
    }
    gInHud=false;
}
uint32_t Hash(const char *s) { uint32_t h=2166136261u;for(;*s;++s)h=(h^uint8_t(*s))*16777619u;return h; }
bool InstallHud() {
    if(gNextHud)return true;
    const auto *mh=reinterpret_cast<const mach_header_64 *>(gMain);
    unsigned long textSize=0;const uint8_t *text=getsectiondata(mh,"__TEXT","__text",&textSize);
    if(!text)return false;
    const uint8_t *p=reinterpret_cast<const uint8_t *>(mh+1)+mh->sizeofcmds;
    for(;p+sizeof(Header)<=text;p+=4) {
        if(memcmp(p,"VCMMHK01",8))continue;
        const auto *table=reinterpret_cast<const Header *>(p);
        if(table->version!=2 || table->count>512 || p+sizeof(Header)+table->count*sizeof(Entry)>text || memcmp(table->uuid,kGameUuid,16))return false;
        const auto *entries=reinterpret_cast<const Entry *>(table+1);
        for(uint32_t i=0;i<table->count;++i) {
            const Entry &e=entries[i];if(e.hash!=Hash("_ZN4CHud4DrawEv") || e.kind)continue;
            // Validate the patched branch and trampoline exactly as hooks.cpp does.
            uint32_t instruction;memcpy(&instruction,gMain+e.func,4);
            if((instruction&0xfc000000)!=0x14000000)return false;
            const int32_t imm=int32_t(instruction<<6)>>6;
            if(gMain+e.func+intptr_t(imm)*4+16!=gMain+e.original)return false;
            auto **slot=reinterpret_cast<void **>(const_cast<uint8_t *>(gMain)+e.slot);
            void *current=__atomic_load_n(slot,__ATOMIC_ACQUIRE);
            if(!current || current==reinterpret_cast<void *>(HudDraw))return false;
            Dl_info info{};if(!dladdr(current,&info) || !info.dli_fname || !strstr(info.dli_fname,"libVCModernHUD.dylib"))return false;
            // Save CURRENT plugin handler; never use the vanilla trampoline.
            gNextHud=reinterpret_cast<Hud>(current);
            if(!__atomic_compare_exchange_n(slot,&current,reinterpret_cast<void *>(HudDraw),false,__ATOMIC_RELEASE,__ATOMIC_RELAXED)) { gNextHud=nullptr;return false; }
            gHudSlot=slot;PluginGlobals();gResult="HUD chain installed; waiting for loaded gameplay";
            Log("chained current CHud::Draw handler %p at slot %p; native commands run only in this main-thread boundary",reinterpret_cast<void *>(gNextHud),slot);return true;
        }
        return false;
    }
    return false;
}
void EventBoundary(CFRunLoopObserverRef, CFRunLoopActivity, void *) {
    if(!pthread_main_np() || gInHud)return;
    if(!gNextHud) { InstallHud();Poll(nullptr); }
    // Once installed, all commands and status use the HUD boundary exclusively.
}
void Init() {
    // No native game functions, filesystem settings, or save APIs in this constructor.
    const char *command=getenv("VCMM_PROBE_COMMAND");if(!command || command[0]!='/')return;
    gCommand=command;gStatus=gCommand+".status.json";gLog=gCommand+".log";gFrames=gCommand+".frames.csv";
    mach_timebase_info(&gTimebase);
    const auto *main=reinterpret_cast<const mach_header_64 *>(_NSGetMachExecuteHeader());
    if(!main || main->magic!=MH_MAGIC_64)return;
    const uint8_t *uuid=Uuid(main);if(!uuid || memcmp(uuid,kGameUuid,16)) { Log("refused: main executable UUID is not macVC 1.0.5217");return; }
    gMain=reinterpret_cast<const uint8_t *>(main);gSymbols=Resolve();
    Log("loaded pid=%d main_thread=%d symbols=%d command=%s",getpid(),pthread_main_np(),gSymbols,gCommand.c_str());
    if(!gSymbols) { gResult="missing native symbols; native commands disabled";Status(nullptr);return; }
    CFRunLoopObserverRef observer=CFRunLoopObserverCreate(kCFAllocatorDefault,kCFRunLoopBeforeSources|kCFRunLoopBeforeWaiting|kCFRunLoopAfterWaiting,true,0,EventBoundary,nullptr);
    if(observer) { CFRunLoopAddObserver(CFRunLoopGetMain(),observer,kCFRunLoopCommonModes);CFRelease(observer); }
    Status(nullptr);
}
// Ordered C++ initialization keeps the strings above alive before Init uses them.
struct AutoInit { AutoInit() { Init(); } } gAutoInit;
} // namespace
