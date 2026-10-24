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
                           "Wheels=1             ; hold LB: weapon wheel on foot, radio wheel in a vehicle\n"
                           "DpadActions=1        ; D-pad left/right switch weapons / radio stations instead of moving\n"
                           "ModernTriggers=1     ; on foot LT aims and RT fires (instead of switching weapons)\n"
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
