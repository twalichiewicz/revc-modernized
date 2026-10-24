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
	float margin = 0.025f;      // gap to the screen's bottom-left corner, fraction of screen height
	float buildingScale = 0.6f; // multiplier on real building heights
	float size = 1.25f;         // minimap height relative to the vanilla radar
	float aspect = 1.55f;       // width / height
	ViewParams view;
};

extern Config gConfig;

// Directory for our files: ~/Library/Application Support/macVC
std::string SupportDir();

// Reads vcminimap.ini, writing a commented default file if there is none.
void LoadConfig();
