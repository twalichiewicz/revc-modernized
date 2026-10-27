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
	bool mapIcons = true;       // pause map: the cursor snaps to icons, names them, picks one as the destination
	bool circleBack = true;     // Circle (B) goes back in menus instead of Triangle (Y)
	bool wheels = true;         // hold LB: weapon wheel on foot; hold D-pad left/right: radio wheel in a vehicle
	bool dpadActions = true;    // D-pad: weapons / radio, expanded map, side missions (not movement)
	bool modernTriggers = true; // LT aims and RT fires on foot, brake and gas in a vehicle
	bool modernButtons = true;  // GTA V face buttons: B melee / reload; in vehicles B cinematic camera
	bool aimCamera = true;      // over-the-shoulder camera while aiming
	bool driveByAim = true;     // in a vehicle hold LB to aim anywhere, RB shoots
	bool phone = true;          // calls ring in vehicles too; a brick phone shows them, D-pad up answers
	bool rumble = true;         // controller vibration for crashes, explosions, punches
	bool interactions = true;   // prompts on X / B: pick up / wave off hookers, greet / insult people
	bool carRadios = true;      // on foot, hear nearby car radios (and your own after getting out), muffled
	bool radioLogo = true;      // the station's logo at the bottom when it changes, not the big name
	bool places = true;         // map icons for hospitals, police stations and businesses not yet for sale
	bool storyHints = true;     // Ken texts what the story needs once it waits on the businesses; gold rings on the map
	bool credit = true;         // buy a business with a quarter of its price down, the rest out of its income
	bool swim = true;           // Tommy swims instead of drowning
	bool climb = true;          // the jump button vaults, climbs ledges and climbs out of the water
	bool crouchWalk = true;     // crouched, he walks crouched instead of standing up
	bool smarterEnemies = true; // gunmen take cover, shoot over it and move between bursts
	bool bulletTime = true;     // aiming: the right stick's click slows time, jump with the stick dives
	bool heliDamage = true;     // the police helicopter takes hits where it is, smokes, and goes down sooner
	bool heliGunners = true;    // SWAT in the police helicopters' doors (no belly gun), two from 4 stars, the Hunter at 6
	bool copsByCar = true;      // wanted on foot, police arrive by car instead of appearing on foot nearby
	bool smarterPolice = true;  // police units spread over the city, sent by road, more the longer it goes on
	bool roadblocks = true;     // from two stars, roadblocks on the roads out of a ring about you, heavier as it goes on
	bool realisticWanted = true; // police know you by what they've seen: witnesses, your clothes and car, a search, an APB
	bool clouds = true;         // volumetric clouds instead of the flat cloud sprites
	bool modernLighting = true; // bloom, sun shafts, filmic highlights and a touch of contrast
	bool lightGlow = true;      // cars' brake, reverse and headlights glow on the model
	bool steeringWheel = true;  // round steering wheels that turn, his hands on them
	bool firstPerson = true;    // the Select button's cycle on foot goes on to first person
	bool carVariety = true;     // more car models loaded, turned over sooner, fewer of one model at once
	float cameraShake = 1.0f;   // GTA IV-style camera shake (0 = none)
	float spawnDistance = 2.0f; // how much further out cars and people appear (1 = the game's)
	float lookSensitivity = 1.0f; // right stick camera speed
	float aimSensitivity = 0.45f; // ... while aiming or scoped, relative to that
	float margin = 0.025f;      // gap to the screen's bottom-left corner, fraction of screen height
	float buildingScale = 0.6f; // multiplier on real building heights
	float size = 1.25f;         // minimap height relative to the vanilla radar
	float aspect = 1.55f;       // width / height
	ViewParams view;
	std::string modKeys[16]; // [Keys] MOD_ lines as written, by action - bindings::Action::kWeaponWheel
};

extern Config gConfig;

// Directory for our files: ~/Library/Application Support/macVC
std::string SupportDir();

// Reads vcmodernhud.ini, writing a commented default file if there is none.
void LoadConfig();
// Writes one setting into vcmodernhud.ini, the rest of it as it was (the Controls page).
void SaveValue(const char *section, const char *key, const std::string &value);
