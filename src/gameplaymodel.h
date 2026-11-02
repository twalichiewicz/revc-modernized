#pragma once
#include "reticlemodel.h"
#include <cstddef>
#include <cstdint>

namespace gameplaymodel {
constexpr int kPage = 36, kOptions = 27, kControls = 26, kPages = 45, kEntries = 18;
constexpr size_t kPageSize = 752, kEntrySize = 40;
struct Entry {
	int32_t action = 0;
	char key[8]{};
	uint32_t pad = 0;
	uint64_t custom = 0;
	int32_t saveSlot = 0, target = 0;
	uint16_t x = 0, y = 0;
	uint8_t align = 0, tail[3]{};
};
struct Page {
	char key[8]{};
	int32_t parent = 0;
	uint32_t pad = 0;
	uint64_t layout = 0, onBack = 0;
	Entry entries[kEntries]{};
};
static_assert(sizeof(Entry) == kEntrySize && offsetof(Entry, target) == 28 && offsetof(Entry, x) == 32);
static_assert(sizeof(Page) == kPageSize && offsetof(Page, entries) == 32);
struct Registration {
	bool ready = false;
	int entry = -1;
	bool changed = false;
};
// Reserves the verified empty legacy slot36 and inserts Gameplay before Back.
// Existing rows/CFO payloads remain byte-identical; unsafe/full tables remain untouched.
Registration Prepare(uint8_t *screens, size_t bytes);
enum class Input { Up, Down, Left, Right, Select, Back };
struct Focus {
	int row = 0;
};
struct Action {
	bool handled = false;
	reticlemodel::Row row = reticlemodel::Row::kStyle;
	int change = 0;
};
Action Apply(Focus &focus, Input input);
} // namespace gameplaymodel
