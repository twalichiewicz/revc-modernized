#pragma once
// Readers for Vice City's PC data: IMG archives, TXD (DXT1) textures, IPL
// instances and COL1 collision.
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct Vec3f {
	float x, y, z;
};

class ImgArchive {
public:
	bool Open(const std::string &imgPath);
	bool Read(const std::string &name, std::vector<uint8_t> &out) const;
	std::vector<std::string> Names() const;

private:
	std::string path_;
	std::map<std::string, std::pair<uint32_t, uint32_t>> entries_; // lower-case name -> sector offset, count
};

// A TXD's texture as RGBA (DXT1, DXT3 or 8888): the one called `name`
// (any case), or the first for an empty name. False if missing or unsupported.
bool DecodeTxdTexture(const std::vector<uint8_t> &txd, const std::string &name, int &w, int &h,
                      std::vector<uint8_t> &rgba);
bool DecodeTxdFirstTexture(const std::vector<uint8_t> &txd, int &w, int &h, std::vector<uint8_t> &rgba);
bool ReadGameFile(const std::string &path, std::vector<uint8_t> &out);

struct ColFace {
	uint32_t a, b, c;
	uint8_t surface;
};
struct ColBox {
	Vec3f min, max;
	uint8_t surface;
};
struct ColModel {
	std::vector<Vec3f> verts;
	std::vector<ColFace> faces;
	std::vector<ColBox> boxes;
};
// Appends every COL1 entry of an archive, keyed by lower-case model name.
void ParseColArchive(const std::vector<uint8_t> &data, std::map<std::string, ColModel> &out);

struct Instance {
	std::string model; // lower case
	int interior;
	Vec3f pos;
	float qx, qy, qz, qw;
};
// Reads the "inst" sections of every IPL listed in data/gta_vc.dat.
std::vector<Instance> LoadInstances(const std::string &gameData);

// A car path segment from data/maps/paths.ipl, in world units.
struct RoadSegment {
	Vec3f a, b;
	float width; // kerb to kerb: lanes * 5 + median
};
std::vector<RoadSegment> LoadCarPaths(const std::string &gameData);

std::string Lower(std::string s);
bool WritePng(const std::string &path, int w, int h, const std::vector<uint8_t> &rgba);
