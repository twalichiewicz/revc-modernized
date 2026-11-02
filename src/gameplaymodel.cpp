#include "gameplaymodel.h"
#include <algorithm>
#include <cstring>

namespace gameplaymodel {
namespace {
constexpr int kChangeMenu = 4, kGoBack = 35;
bool Key(const char *a, const char *b) { return std::strncmp(a, b, 8) == 0; }
} // namespace
Registration Prepare(uint8_t *screens, size_t bytes)
{
	if (!screens || bytes < kPages * kPageSize)
		return {};
	Page options, spare;
	std::memcpy(&options, screens + kOptions * kPageSize, sizeof options);
	std::memcpy(&spare, screens + kPage * kPageSize, sizeof spare);
	int count = 0, back = -1, existing = -1;
	for (; count < kEntries && options.entries[count].action; count++) {
		if (options.entries[count].action == kGoBack && back < 0)
			back = count;
		if (Key(options.entries[count].key, "FEO_GMP"))
			existing = count;
	}
	if (existing >= 0) {
		const bool own = options.entries[existing].action == kChangeMenu && options.entries[existing].target == kPage &&
		                 Key(spare.key, "FET_GMP") && spare.parent == kOptions && spare.layout == 0 &&
		                 spare.onBack == 0 && spare.entries[0].action == kGoBack;
		return {own, own ? existing : -1, false};
	}
	// Native slot36 is the unused legacy Controller Old2 page, not the
	// keyboard page30 (which has special drawing/input code in the engine).
	if (count >= kEntries || back < 0 || !Key(spare.key, "FET_CTL") || spare.parent != kControls || spare.layout ||
	    spare.onBack)
		return {};
	const Entry empty{};
	for (const Entry &e : spare.entries)
		if (std::memcmp(&e, &empty, sizeof e))
			return {};
	for (int p = 0; p < kPages; p++) {
		Page page;
		std::memcpy(&page, screens + p * kPageSize, sizeof page);
		for (const Entry &e : page.entries)
			if (e.action > 0 && e.target == kPage)
				return {};
	}
	// Validate everything before either native table is changed.
	Page gameplay{};
	std::memcpy(gameplay.key, "FET_GMP", 8);
	gameplay.parent = kOptions;
	gameplay.entries[0] = options.entries[back];
	auto &goBack = gameplay.entries[0];
	goBack.custom = 0;
	goBack.saveSlot = 0;
	goBack.target = kOptions;
	goBack.x = 320;
	goBack.y = 396;
	goBack.align = 3;
	for (int i = count; i > back; i--)
		options.entries[i] = options.entries[i - 1];
	Entry entry{};
	entry.action = kChangeMenu;
	std::memcpy(entry.key, "FEO_GMP", 8);
	entry.target = kPage;
	entry.align = 3;
	options.entries[back] = entry;
	std::memcpy(screens + kPage * kPageSize, &gameplay, sizeof gameplay);
	std::memcpy(screens + kOptions * kPageSize, &options, sizeof options);
	return {true, back, true};
}
Action Apply(Focus &focus, Input input)
{
	constexpr int count = int(reticlemodel::Row::kCount);
	focus.row = std::clamp(focus.row, 0, count - 1);
	if (input == Input::Back)
		return {};
	if (input == Input::Up)
		focus.row = (focus.row + count - 1) % count;
	else if (input == Input::Down)
		focus.row = (focus.row + 1) % count;
	const int change = input == Input::Left ? -1 : (input == Input::Right || input == Input::Select) ? 1 : 0;
	return {true, reticlemodel::Row(focus.row), change};
}
} // namespace gameplaymodel
