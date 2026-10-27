#pragma once
// A hook's name in the patch table (version 2): its 32-bit FNV-1a hash, as
// tools/patch_macvc.py's name_hash computes it. Unit-tested
// (tests/test_hookname.cpp, against the patcher's own value).
#include <cstdint>

namespace hooks {

constexpr uint32_t NameHash(const char *name)
{
	uint32_t h = 0x811c9dc5u;
	for (; *name; name++)
		h = (h ^ uint8_t(*name)) * 0x01000193u;
	return h;
}

} // namespace hooks
