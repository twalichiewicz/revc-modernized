#pragma once
#include "projection.h"

#include <string>

struct Config {
	bool enabled = true;
	bool gps = true;
	bool autoClearWaypoint = true;
	bool buildings = true;
	bool bars = true;           // health/armour/stamina under the map (hides the top-right ones)
	bool hud = true;            // clock on the map, no money, small weapon/stars repositioned
	bool moneyPill = true;      // money next to the map where it can be spent
	bool pauseTabs = true;      // pause menu opens on the map, other pages as tabs
	bool compactLegend = true;  // pause map legend shrunk into a corner
	bool circleBack = true;     // Circle (B) goes back in menus instead of Triangle (Y)
	bool wheels = true;         // hold LB: weapon wheel on foot; hold D-pad left/right: radio wheel in a vehicle
	bool dpadActions = true;    // D-pad: weapons / radio, expanded map, side missions (not movement)
	bool modernTriggers = true; // LT aims and RT fires on foot, brake and gas in a vehicle
	bool aimCamera = true;      // over-the-shoulder camera while aiming
	bool driveByAim = true;     // in a vehicle hold LB to aim anywhere, RB shoots
	bool phone = true;          // calls ring in vehicles too; a brick phone shows them, D-pad up answers
	float lookSensitivity = 1.0f; // right stick camera speed
	float aimSensitivity = 0.45f; // ... while aiming or scoped, relative to that
	float margin = 0.025f;      // gap to the screen's bottom-left corner, fraction of screen height
	float buildingScale = 0.6f; // multiplier on real building heights
	float size = 1.25f;         // minimap height relative to the vanilla radar
	float aspect = 1.55f;       // width / height
	ViewParams view;
};

extern Config gConfig;

// Directory for our files: ~/Library/Application Support/macVC
std::string SupportDir();

// Reads vcmodernhud.ini, writing a commented default file if there is none.
void LoadConfig();
