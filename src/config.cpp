#include "config.h"
#include "log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

Config gConfig;

namespace {

const char kDefaultIni[] = "; VC Modern HUD for macVC. Delete this file to restore defaults.\n"
                           "[Minimap]\n"
                           "Enabled=1            ; 0 = the original round radar\n"
                           "GPS=1                ; route line to your waypoint / mission objective\n"
                           "AutoClearWaypoint=1  ; remove the waypoint when you arrive\n"
                           "Buildings=1          ; 3D buildings\n"
                           "BuildingHeight=0.6   ; 1 = true height\n"
                           "Bars=1               ; health/armour/stamina under the map\n"
                           "Hud=1                ; clock on the map, no money, small weapon + stars\n"
                           "MoneyPill=1          ; show money next to the map in shops / at property for sale\n"
                           "PauseTabs=1          ; pause menu opens on the map; Q/E or LB/RB switch tabs\n"
                           "CompactLegend=1      ; small map legend in the corner of the pause map\n"
                           "CircleBack=1         ; Circle (B) goes back in menus; 0 = Triangle (Y), as on the PS2\n"
                           "Wheels=1             ; hold LB: weapon wheel on foot; hold D-pad left/right: radio wheel\n"
                           "DpadActions=1        ; D-pad: left/right weapons or radio, down big map, up side missions\n"
                           "ModernTriggers=1     ; LT aims, RT fires; in a vehicle RT gas, LT brake, R3 look back\n"
                           "ModernButtons=1      ; GTA V face buttons: B melees / reloads; in a vehicle hold B for the cinematic camera\n"
                           "AimCamera=1          ; over-the-shoulder camera while aiming\n"
                           "DriveByAim=1         ; in a vehicle hold LB to aim an SMG or pistol anywhere, RB shoots\n"
                           "Phone=1              ; calls ring in vehicles too: a brick phone slides up, D-pad up answers\n"
                           "Rumble=1             ; controller vibration (crashes, explosions, punches)\n"
                           "Interactions=1       ; X/B (Square/Circle): pick up or wave off hookers, greet or insult people\n"
                           "CarRadios=1          ; on foot, hear car radios muffled: yours after you get out, stopped cars nearby\n"
                           "RadioLogo=1          ; station logo at the bottom centre when it changes, instead of the big name\n"
                           "Places=1             ; map icons: hospitals, police stations, businesses before they're for sale\n"
                           "StoryHints=1         ; Ken texts what the story needs from your businesses; gold rings on the map\n"
                           "Credit=1             ; buy a business with a quarter down, the rest paid from your businesses' income\n"
                           "Swimming=1           ; Tommy swims instead of drowning\n"
                           "Climbing=1           ; jump vaults low walls, climbs ledges and climbs out of the water\n"
                           "CrouchWalk=1         ; crouched, Tommy walks crouched instead of standing up\n"
                           "SmarterEnemies=1     ; gunmen take cover, shoot over it and move about between bursts\n"
                           "BulletTime=1         ; aiming: R3 slows time (uses stamina); jump with the stick dives\n"
                           "HeliDamage=1         ; the police helicopter takes hits where it is, smokes, and goes down sooner\n"
                           "HeliGunners=1        ; police helicopters with SWAT door gunners instead of a belly gun, two from 4 stars, the Hunter at 6\n"
                           "CopsByCar=1          ; wanted on foot, police arrive by car instead of appearing on foot nearby\n"
                           "SmarterPolice=1      ; police units all over the city sent to you by road, more the longer it lasts\n"
                           "Roadblocks=1         ; from two stars, roadblocks round where you are, more and heavier as it goes on\n"
                           "RealisticWanted=1    ; police know you by what they've seen: witnesses, your clothes and car, a search, an APB until you save\n"
                           "Clouds=1             ; volumetric clouds instead of the flat cloud sprites\n"
                           "ModernLighting=1     ; bloom, sun shafts, filmic highlights and a touch of contrast\n"
                           "LightGlow=1          ; cars' brake, reverse and headlights glow on the model\n"
                           "SteeringWheel=1      ; round steering wheels that turn, Tommy's hands on them\n"
                           "FirstPerson=1        ; on foot, Select (Back) goes near, mid, far, then first person\n"
                           "CameraShake=1        ; handheld camera on foot, sway and jolts in vehicles (0 = none, 2 = double)\n"
                           "SpawnDistance=2      ; cars and people appear this much further out, past where you notice (1 = as the game)\n"
                           "CarVariety=1         ; more car models about, and fewer of the same one at once\n"
                           "LookSensitivity=1    ; right stick camera speed\n"
                           "AimSensitivity=0.45  ; right stick speed while aiming or scoped (1 = as when not aiming)\n"
                           "Margin=0.025         ; distance from the screen corner\n"
                           "Tilt=50              ; degrees of 3D tilt, 0 = flat\n"
                           "Zoom=1.5             ; higher = closer\n"
                           "Size=1.25            ; height relative to the original radar\n"
                           "Aspect=1.55          ; width / height\n";

void Apply(const char *key, const char *value)
{
	const float f = static_cast<float>(atof(value));
	const bool b = atoi(value) != 0;
	if (!strcasecmp(key, "Enabled"))
		gConfig.enabled = b;
	else if (!strcasecmp(key, "GPS"))
		gConfig.gps = b;
	else if (!strcasecmp(key, "AutoClearWaypoint"))
		gConfig.autoClearWaypoint = b;
	else if (!strcasecmp(key, "Buildings"))
		gConfig.buildings = b;
	else if (!strcasecmp(key, "BuildingHeight") && f >= 0.0f)
		gConfig.buildingScale = f;
	else if (!strcasecmp(key, "Bars"))
		gConfig.bars = b;
	else if (!strcasecmp(key, "Hud"))
		gConfig.hud = b;
	else if (!strcasecmp(key, "MoneyPill"))
		gConfig.moneyPill = b;
	else if (!strcasecmp(key, "PauseTabs"))
		gConfig.pauseTabs = b;
	else if (!strcasecmp(key, "CompactLegend"))
		gConfig.compactLegend = b;
	else if (!strcasecmp(key, "CircleBack"))
		gConfig.circleBack = b;
	else if (!strcasecmp(key, "Wheels"))
		gConfig.wheels = b;
	else if (!strcasecmp(key, "DpadActions"))
		gConfig.dpadActions = b;
	else if (!strcasecmp(key, "ModernTriggers"))
		gConfig.modernTriggers = b;
	else if (!strcasecmp(key, "ModernButtons"))
		gConfig.modernButtons = b;
	else if (!strcasecmp(key, "AimCamera"))
		gConfig.aimCamera = b;
	else if (!strcasecmp(key, "DriveByAim"))
		gConfig.driveByAim = b;
	else if (!strcasecmp(key, "Phone"))
		gConfig.phone = b;
	else if (!strcasecmp(key, "Rumble"))
		gConfig.rumble = b;
	else if (!strcasecmp(key, "Interactions"))
		gConfig.interactions = b;
	else if (!strcasecmp(key, "CarRadios"))
		gConfig.carRadios = b;
	else if (!strcasecmp(key, "RadioLogo"))
		gConfig.radioLogo = b;
	else if (!strcasecmp(key, "Places"))
		gConfig.places = b;
	else if (!strcasecmp(key, "StoryHints"))
		gConfig.storyHints = b;
	else if (!strcasecmp(key, "Credit"))
		gConfig.credit = b;
	else if (!strcasecmp(key, "Swimming"))
		gConfig.swim = b;
	else if (!strcasecmp(key, "Climbing"))
		gConfig.climb = b;
	else if (!strcasecmp(key, "CrouchWalk"))
		gConfig.crouchWalk = b;
	else if (!strcasecmp(key, "SmarterEnemies"))
		gConfig.smarterEnemies = b;
	else if (!strcasecmp(key, "BulletTime"))
		gConfig.bulletTime = b;
	else if (!strcasecmp(key, "HeliDamage"))
		gConfig.heliDamage = b;
	else if (!strcasecmp(key, "HeliGunners"))
		gConfig.heliGunners = b;
	else if (!strcasecmp(key, "CopsByCar"))
		gConfig.copsByCar = b;
	else if (!strcasecmp(key, "SmarterPolice"))
		gConfig.smarterPolice = b;
	else if (!strcasecmp(key, "Roadblocks"))
		gConfig.roadblocks = b;
	else if (!strcasecmp(key, "RealisticWanted"))
		gConfig.realisticWanted = b;
	else if (!strcasecmp(key, "FirstPerson"))
		gConfig.firstPerson = b;
	else if (!strcasecmp(key, "CameraShake"))
		gConfig.cameraShake = f;
	else if (!strcasecmp(key, "SpawnDistance"))
		gConfig.spawnDistance = f;
	else if (!strcasecmp(key, "CarVariety"))
		gConfig.carVariety = b;
	else if (!strcasecmp(key, "Clouds"))
		gConfig.clouds = b;
	else if (!strcasecmp(key, "ModernLighting"))
		gConfig.modernLighting = b;
	else if (!strcasecmp(key, "LightGlow"))
		gConfig.lightGlow = b;
	else if (!strcasecmp(key, "SteeringWheel"))
		gConfig.steeringWheel = b;
	else if (!strcasecmp(key, "AimSensitivity") && f > 0.05f && f <= 2.0f)
		gConfig.aimSensitivity = f;
	else if (!strcasecmp(key, "LookSensitivity") && f > 0.05f && f <= 4.0f)
		gConfig.lookSensitivity = f;
	else if (!strcasecmp(key, "Margin") && f >= 0.0f && f < 0.3f)
		gConfig.margin = f;
	else if (!strcasecmp(key, "Tilt"))
		gConfig.view.tiltDeg = f;
	else if (!strcasecmp(key, "Zoom") && f > 0.1f)
		gConfig.view.zoom = f;
	else if (!strcasecmp(key, "Size") && f > 0.3f)
		gConfig.size = f;
	else if (!strcasecmp(key, "Aspect") && f > 0.5f)
		gConfig.aspect = f;
	else
		Log("config: unknown key %s", key);
}

char *Trim(char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	char *end = s + strlen(s);
	while (end > s && strchr(" \t\r\n", end[-1]))
		*--end = '\0';
	return s;
}

} // namespace

std::string SupportDir()
{
	const char *home = getenv("HOME");
	return std::string(home ? home : "") + "/Library/Application Support/macVC";
}

void LoadConfig()
{
	const std::string path = SupportDir() + "/vcmodernhud.ini";
	FILE *f = fopen(path.c_str(), "r");
	if (!f) {
		if (FILE *out = fopen(path.c_str(), "w")) {
			fputs(kDefaultIni, out);
			fclose(out);
		}
		Log("config: wrote defaults to %s", path.c_str());
		return;
	}
	char line[256];
	while (fgets(line, sizeof line, f)) {
		if (char *comment = strchr(line, ';'))
			*comment = '\0';
		char *eq = strchr(line, '=');
		if (!eq)
			continue;
		*eq = '\0';
		Apply(Trim(line), Trim(eq + 1));
	}
	fclose(f);
	Log("config: enabled=%d gps=%d buildings=%d x%.2f tilt=%.0f zoom=%.2f size=%.2f aspect=%.2f", gConfig.enabled,
	    gConfig.gps, gConfig.buildings, gConfig.buildingScale, gConfig.view.tiltDeg, gConfig.view.zoom, gConfig.size,
	    gConfig.aspect);
}
