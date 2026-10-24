#include "radiologos.h"

#include <cstdio>
#include <cstring>

namespace radiologos {
namespace {

constexpr char kMagic[8] = {'V', 'C', 'H', 'U', 'D', 'R', 'L', '1'};
constexpr size_t kBytes = size_t(kSize) * kSize * 4;

} // namespace

// "VCHUDRL1", then per station a present byte and, if present, the pixels.
bool Save(const std::string &path, const Logos &logos)
{
	FILE *f = fopen(path.c_str(), "wb");
	if (!f)
		return false;
	bool ok = fwrite(kMagic, 1, sizeof kMagic, f) == sizeof kMagic;
	for (int i = 0; i < kCount && ok; i++) {
		const bool present = i < int(logos.size()) && logos[i].size() == kBytes;
		const uint8_t flag = present;
		ok = fwrite(&flag, 1, 1, f) == 1 && (!present || fwrite(logos[i].data(), 1, kBytes, f) == kBytes);
	}
	return fclose(f) == 0 && ok;
}

bool Load(const std::string &path, Logos &logos)
{
	FILE *f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	char magic[8];
	bool ok = fread(magic, 1, sizeof magic, f) == sizeof magic && memcmp(magic, kMagic, sizeof magic) == 0;
	logos.assign(kCount, {});
	for (int i = 0; i < kCount && ok; i++) {
		uint8_t flag = 0;
		ok = fread(&flag, 1, 1, f) == 1;
		if (ok && flag) {
			logos[i].resize(kBytes);
			ok = fread(logos[i].data(), 1, kBytes, f) == kBytes;
		}
	}
	fclose(f);
	return ok;
}

} // namespace radiologos
