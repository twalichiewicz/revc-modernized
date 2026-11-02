#include "gameplaymodel.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace gameplaymodel;
namespace {
std::vector<uint8_t> Table()
{
	std::vector<uint8_t> bytes(kPages * kPageSize);
	Page spare;
	std::memcpy(spare.key, "FET_CTL", 8);
	spare.parent = kControls;
	std::memcpy(bytes.data() + kPage * kPageSize, &spare, sizeof spare);
	Page options;
	std::memcpy(options.key, "FET_OPT", 8);
	options.parent = 33;
	const int targets[] = {26, 3, 4, 40, 5, 21, 0};
	const char *keys[] = {"FEO_CON", "FEO_AUD", "FEO_DIS", "FET_GFX", "FEO_LAN", "FET_PS", "FEDS_TB"};
	for (int i = 0; i < 7; i++) {
		auto &e = options.entries[i];
		e.action = i == 6 ? 35 : 4;
		e.target = targets[i];
		e.align = 3;
		std::strncpy(e.key, keys[i], 8);
	}
	options.entries[0].x = 320;
	options.entries[0].y = 132;
	// Include a native custom option's opaque payload among the preserved rows.
	options.entries[3].action = -2;
	options.entries[3].custom = 0x123456789abcdef0ull;
	std::memcpy(bytes.data() + kOptions * kPageSize, &options, sizeof options);
	bytes[10 * kPageSize] = 0x42;
	return bytes;
}
Page Read(const std::vector<uint8_t> &bytes, int page)
{
	Page p;
	std::memcpy(&p, bytes.data() + page * kPageSize, sizeof p);
	return p;
}
void InsertAndBack()
{
	auto bytes = Table(), before = bytes;
	const Page old = Read(bytes, kOptions);
	const auto result = Prepare(bytes.data(), bytes.size());
	assert(result.ready && result.changed && result.entry == 6);
	const Page options = Read(bytes, kOptions), page = Read(bytes, kPage);
	assert(options.entries[6].action == 4 && options.entries[6].target == kPage &&
	       !std::strcmp(options.entries[6].key, "FEO_GMP"));
	for (int i = 0; i < 6; i++)
		assert(!std::memcmp(&old.entries[i], &options.entries[i], sizeof(Entry)));
	assert(!std::memcmp(&old.entries[6], &options.entries[7], sizeof(Entry)));
	assert(!std::strcmp(page.key, "FET_GMP") && page.parent == kOptions && !page.layout && !page.onBack);
	assert(page.entries[0].action == 35 && page.entries[0].target == kOptions && page.entries[0].y < 430);
	// Native SwitchToNewScreen(-2) follows this parent and scans its target rows.
	const Page parent = Read(bytes, page.parent);
	int selected = -1;
	for (int i = 0; i < kEntries; i++)
		if (parent.entries[i].action >= 0 && parent.entries[i].target == kPage) {
			selected = i;
			break;
		}
	assert(selected == result.entry && page.parent == 27);
	for (int p = 0; p < kPages; p++)
		if (p != kOptions && p != kPage)
			assert(!std::memcmp(bytes.data() + p * kPageSize, before.data() + p * kPageSize, kPageSize));
	const auto once = bytes;
	const auto again = Prepare(bytes.data(), bytes.size());
	assert(again.ready && !again.changed && bytes == once);
}
void GuardedReservation()
{
	for (int kind = 0; kind < 4; kind++) {
		auto bytes = Table();
		Page options = Read(bytes, kOptions), spare = Read(bytes, kPage);
		if (kind == 0)
			spare.entries[0].action = 35;
		if (kind == 1)
			spare.layout = 1;
		if (kind == 2) {
			Page other;
			other.entries[0].action = 4;
			other.entries[0].target = kPage;
			std::memcpy(bytes.data() + 2 * kPageSize, &other, sizeof other);
		}
		if (kind == 3)
			for (auto &entry : options.entries)
				entry.action = 4;
		std::memcpy(bytes.data() + kOptions * kPageSize, &options, sizeof options);
		std::memcpy(bytes.data() + kPage * kPageSize, &spare, sizeof spare);
		auto before = bytes;
		assert(!Prepare(bytes.data(), bytes.size()).ready && bytes == before);
	}
}
void FocusAndChanges()
{
	Focus f;
	auto back = Apply(f, Input::Back);
	assert(!back.handled && !back.change && f.row == 0);
	Apply(f, Input::Up);
	assert(f.row == int(reticlemodel::Row::kDefaults));
	Apply(f, Input::Down);
	assert(f.row == 0);
	const auto right = Apply(f, Input::Right);
	assert(right.handled && right.change == 1 && right.row == reticlemodel::Row::kStyle);
	assert(Apply(f, Input::Left).change == -1 && Apply(f, Input::Select).change == 1);
}
} // namespace
int main()
{
	InsertAndBack();
	GuardedReservation();
	FocusAndChanges();
	puts("all Gameplay menu table and navigation tests passed");
}
