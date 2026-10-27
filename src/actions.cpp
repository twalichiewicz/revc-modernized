#include "actions.h"
#include "config.h"
#include "game.h"
#include "log.h"

#include <string>

using namespace game;
using bindings::Action;

namespace actions {
namespace {

constexpr int kMods = int(Action::kModEnd) - int(Action::kWeaponWheel);
constexpr int kSettings = 0xec0;   // ControlsManager.m_aSettings: 32 bytes an action

keys::Binding gKeys[kMods];
bool gLoaded;
uint32_t gVersion;
bool gHeld[kMods], gWas[kMods];

bool IsMod(Action a) { return a >= Action::kWeaponWheel && a < Action::kModEnd; }
int Index(Action a) { return int(a) - int(Action::kWeaponWheel); }

// m_aSettings[action][type].m_Key: type 0 the keyboard key, 1 the extra one, 2 the mouse button.
int32_t Setting(Action a, int type)
{
	return *reinterpret_cast<const int32_t *>(api.controlsManager + kSettings + 32 * int(a) + 8 * type);
}

// All of a game action's keys, the mouse button too (a Binding keeps two).
int GameCodes(Action a, uint16_t codes[3])
{
	if (!api.controlsManager || a >= Action::kGameCount)
		return 0;
	return keys::GameCodes(Setting(a, 0), Setting(a, 1), Setting(a, 2), codes);
}

bool Has(const keys::Binding &b, uint16_t key) { return key != keys::kNone && (b.key[0] == key || b.key[1] == key); }

// The ini's keys, else the defaults, once the game has read its own bindings (macVC.ini [Bindings]).
void Load()
{
	auto gameUses = [](uint16_t key, bindings::Scope scope) {
		for (int i = 0; i < int(Action::kGameCount); i++)
			if (bindings::ScopeOf(Action(i)) == scope && Has(GameKeys(Action(i)), key))
				return true;
		return false;
	};
	std::string summary;
	for (int i = 0; i < kMods; i++) {
		const Action a = Action(int(Action::kWeaponWheel) + i);
		if (!bindings::HasOwnKeys(a))
			continue;
		const std::string &line = gConfig.modKeys[i];
		gKeys[i] = line.empty() ? bindings::DefaultKeys(a, gameUses) : keys::Parse(line);
		summary += std::string(" ") + bindings::Name(a) + "=" + keys::Format(gKeys[i]);
	}
	gLoaded = true;
	gVersion++;
	Log("actions: keys%s", summary.c_str());
}

bool Down(uint16_t key)
{
	if (key == keys::kNone)
		return false;
	if (key > keys::kMouseBase)
		return api.GetIsMouseButtonDown(api.controlsManager, key - keys::kMouseBase);
	return api.GetIsKeyboardKeyDown(api.controlsManager, key);
}

} // namespace

keys::Binding GameKeys(Action a)
{
	keys::Binding b;
	uint16_t codes[3];
	const int n = GameCodes(a, codes);
	for (int i = 0; i < n && i < 2; i++)
		b.key[i] = codes[i];
	return b;
}

std::array<uint16_t, 3> GameSlots(Action a)
{
	std::array<uint16_t, 3> slots{};
	if (!api.controlsManager || a >= Action::kGameCount)
		return slots;
	for (int type = 0; type < 3; type++) {
		uint16_t code[3];
		const int32_t setting = Setting(a, type);
		if (keys::GameCodes(type < 2 ? setting : 0, 0, type == 2 ? setting : 0, code) == 1)
			slots[size_t(type)] = code[0];
	}
	return slots;
}

keys::Binding Keys(Action a)
{
	Action game;
	if (bindings::KeyAlias(a, game))
		return GameKeys(game);
	if (IsMod(a))
		return gLoaded ? gKeys[Index(a)] : bindings::DefaultKeys(a, [](uint16_t, bindings::Scope) { return false; });
	return GameKeys(a);
}

void LoadKeys()
{
	if (!gLoaded && api.controlsManager)
		Load();
}

void SetKeys(Action a, const keys::Binding &b)
{
	if (!bindings::HasOwnKeys(a))
		return;
	LoadKeys(); // else a later Load() would put the ini's old line back
	gKeys[Index(a)] = b;
	gVersion++;
	// "none": no key (an empty line would mean the default)
	gConfig.modKeys[Index(a)] = b.Empty() ? "none" : keys::Format(b);
	SaveValue("Keys", bindings::Name(a), gConfig.modKeys[Index(a)]);
}

void GameKeysChanged() { gVersion++; }

uint32_t Version() { return gVersion; }

void Update()
{
	if (!api.controlsManager || !api.GetIsKeyboardKeyDown || !api.GetIsMouseButtonDown)
		return;
	if (!gLoaded)
		Load();
	for (int i = 0; i < kMods; i++) {
		gWas[i] = gHeld[i];
		const Action a = Action(int(Action::kWeaponWheel) + i);
		Action game;
		if (bindings::KeyAlias(a, game)) { // all the game action's keys (LMB fires a drive-by)
			uint16_t codes[3];
			const int n = GameCodes(game, codes);
			gHeld[i] = false;
			for (int k = 0; k < n; k++)
				gHeld[i] = gHeld[i] || Down(codes[k]);
		} else {
			const keys::Binding b = Keys(a);
			gHeld[i] = Down(b.key[0]) || Down(b.key[1]);
		}
	}
}

bool KeyHeld(Action a) { return IsMod(a) && gHeld[Index(a)]; }
bool KeyPressed(Action a) { return IsMod(a) && gHeld[Index(a)] && !gWas[Index(a)]; }
bool KeyReleased(Action a) { return IsMod(a) && !gHeld[Index(a)] && gWas[Index(a)]; }

} // namespace actions
