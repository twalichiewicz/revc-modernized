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
	bool wheels = true;         // hold LB: weapon wheel on foot, radio wheel in a vehicle
	bool dpadActions = true;    // D-pad left/right: weapons on foot, radio in a vehicle (not movement)
	bool modernTriggers = true; // on foot LT aims and RT fires, instead of cycling weapons
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
