#pragma once
// The radio stations' logos for the radio wheel, cut from the frontend's
// textures (models/fronten2.txd, 22 MB) by tools/bake at install time.
#include <cstdint>
#include <string>
#include <vector>

namespace radiologos {

constexpr int kSize = 128; // square RGBA
constexpr int kCount = 9;  // stations 0-8
// Texture names in fronten2.txd, by station.
constexpr const char *kTextures[kCount] = {"wildstyle", "flash", "kchat",     "fever",  "vrock",
                                          "vcpr",      "espantoso", "emotion", "wave103"};

// kCount images of kSize x kSize RGBA; an empty image means "missing".
using Logos = std::vector<std::vector<uint8_t>>;

bool Save(const std::string &path, const Logos &logos);
bool Load(const std::string &path, Logos &logos);

} // namespace radiologos
