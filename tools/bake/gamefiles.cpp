#include "gamefiles.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <zlib.h>

namespace {

constexpr uint32_t kSector = 2048;

template <typename T>
T Get(const std::vector<uint8_t> &d, size_t off)
{
	T v{};
	if (off + sizeof(T) <= d.size())
		memcpy(&v, d.data() + off, sizeof(T));
	return v;
}

bool ReadFile(const std::string &path, std::vector<uint8_t> &out)
{
	std::ifstream f(path, std::ios::binary);
	if (!f)
		return false;
	out.assign(std::istreambuf_iterator<char>(f), {});
	return true;
}

// A DXT colour block (4 bytes of endpoints, 4 of 2-bit indices). DXT3 always
// uses the four-colour mode; DXT1 switches to three colours + transparent
// when the first endpoint is not the larger one.
void DecodeColourBlock(const uint8_t *src, bool fourColour, uint8_t pal[4][4], uint32_t &bits)
{
	uint16_t c[2];
	memcpy(c, src, 4);
	memcpy(&bits, src + 4, 4);
	for (int i = 0; i < 2; i++) {
		pal[i][0] = uint8_t((c[i] >> 11) * 255 / 31);
		pal[i][1] = uint8_t(((c[i] >> 5) & 63) * 255 / 63);
		pal[i][2] = uint8_t((c[i] & 31) * 255 / 31);
		pal[i][3] = 255;
	}
	const bool four = fourColour || c[0] > c[1];
	for (int k = 0; k < 3; k++) {
		if (four) {
			pal[2][k] = uint8_t((2 * pal[0][k] + pal[1][k]) / 3);
			pal[3][k] = uint8_t((pal[0][k] + 2 * pal[1][k]) / 3);
		} else {
			pal[2][k] = uint8_t((pal[0][k] + pal[1][k]) / 2);
			pal[3][k] = 0;
		}
	}
	pal[2][3] = 255;
	pal[3][3] = four ? 255 : 0;
}

// DXT1 (8-byte blocks) or DXT3 (8 bytes of 4-bit alpha, then a colour block).
void DecodeDxt(const uint8_t *src, int w, int h, bool dxt3, std::vector<uint8_t> &out)
{
	out.assign(size_t(w) * h * 4, 0);
	for (int by = 0; by < h; by += 4)
		for (int bx = 0; bx < w; bx += 4) {
			uint64_t alpha = 0;
			if (dxt3) {
				memcpy(&alpha, src, 8);
				src += 8;
			}
			uint8_t pal[4][4];
			uint32_t bits;
			DecodeColourBlock(src, dxt3, pal, bits);
			src += 8;
			for (int py = 0; py < 4; py++)
				for (int px = 0; px < 4; px++) {
					const int x = bx + px, y = by + py, i = py * 4 + px;
					if (x >= w || y >= h)
						continue;
					uint8_t *p = &out[(size_t(y) * w + x) * 4];
					memcpy(p, pal[(bits >> (2 * i)) & 3], 4);
					if (dxt3)
						p[3] = uint8_t(((alpha >> (4 * i)) & 15) * 17);
				}
		}
}

} // namespace

std::string Lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
	return s;
}

bool ImgArchive::Open(const std::string &imgPath)
{
	path_ = imgPath;
	std::vector<uint8_t> dir;
	if (!ReadFile(imgPath.substr(0, imgPath.size() - 4) + ".dir", dir))
		return false;
	for (size_t i = 0; i + 32 <= dir.size(); i += 32) {
		char name[25] = {};
		memcpy(name, &dir[i + 8], 24);
		entries_[Lower(name)] = {Get<uint32_t>(dir, i), Get<uint32_t>(dir, i + 4)};
	}
	return !entries_.empty();
}

bool ImgArchive::Read(const std::string &name, std::vector<uint8_t> &out) const
{
	auto it = entries_.find(Lower(name));
	if (it == entries_.end())
		return false;
	std::ifstream f(path_, std::ios::binary);
	f.seekg(std::streamoff(it->second.first) * kSector);
	out.resize(size_t(it->second.second) * kSector);
	f.read(reinterpret_cast<char *>(out.data()), std::streamsize(out.size()));
	return bool(f);
}

std::vector<std::string> ImgArchive::Names() const
{
	std::vector<std::string> names;
	for (auto &e : entries_)
		names.push_back(e.first);
	return names;
}

bool DecodeTxdTexture(const std::vector<uint8_t> &d, const std::string &name, int &w, int &h,
                      std::vector<uint8_t> &rgba)
{
	// TXD header, struct (count + device), then one texture native chunk per texture.
	for (size_t tex = 12 + 12 + 4; Get<uint32_t>(d, tex) == 0x15; tex += 12 + Get<uint32_t>(d, tex + 4)) {
		const size_t s = tex + 24; // texture native header + struct header
		char texName[33] = {};
		if (s + 40 <= d.size())
			memcpy(texName, d.data() + s + 8, 32);
		if (!name.empty() && Lower(texName) != Lower(name))
			continue;
		const uint32_t d3dFormat = Get<uint32_t>(d, s + 76);
		w = Get<uint16_t>(d, s + 80);
		h = Get<uint16_t>(d, s + 82);
		const uint8_t depth = d[s + 84];
		const uint8_t compression = d[s + 87];
		const uint32_t size = Get<uint32_t>(d, s + 88);
		const uint8_t *data = d.data() + s + 92;
		if (s + 92 + size > d.size())
			return false;
		if (compression == 1 || compression == 3 || d3dFormat == 0x31545844 || d3dFormat == 0x33545844) {
			DecodeDxt(data, w, h, compression == 3 || d3dFormat == 0x33545844, rgba);
			return true;
		}
		if (depth == 32) {
			rgba.resize(size_t(w) * h * 4);
			for (size_t i = 0; i < rgba.size(); i += 4) {
				rgba[i] = data[i + 2];
				rgba[i + 1] = data[i + 1];
				rgba[i + 2] = data[i];
				rgba[i + 3] = data[i + 3];
			}
			return true;
		}
		return false;
	}
	return false;
}

bool DecodeTxdFirstTexture(const std::vector<uint8_t> &d, int &w, int &h, std::vector<uint8_t> &rgba)
{
	return DecodeTxdTexture(d, "", w, h, rgba);
}

bool ReadGameFile(const std::string &path, std::vector<uint8_t> &out) { return ReadFile(path, out); }

void ParseColArchive(const std::vector<uint8_t> &d, std::map<std::string, ColModel> &out)
{
	size_t off = 0;
	while (off + 8 <= d.size() && memcmp(&d[off], "COLL", 4) == 0) {
		const uint32_t size = Get<uint32_t>(d, off + 4);
		char name[23] = {};
		memcpy(name, &d[off + 8], 22);
		ColModel m;
		size_t p = off + 32 + 40; // name/id, bounds
		p += 4 + size_t(Get<uint32_t>(d, p)) * 20; // spheres
		p += 4 + size_t(Get<uint32_t>(d, p)) * 16; // lines
		uint32_t n = Get<uint32_t>(d, p);
		p += 4;
		for (uint32_t i = 0; i < n; i++, p += 28)
			m.boxes.push_back({Get<Vec3f>(d, p), Get<Vec3f>(d, p + 12), d[p + 24]});
		n = Get<uint32_t>(d, p);
		p += 4;
		for (uint32_t i = 0; i < n; i++, p += 12)
			m.verts.push_back(Get<Vec3f>(d, p));
		n = Get<uint32_t>(d, p);
		p += 4;
		for (uint32_t i = 0; i < n; i++, p += 16)
			m.faces.push_back({Get<uint32_t>(d, p), Get<uint32_t>(d, p + 4), Get<uint32_t>(d, p + 8), d[p + 12]});
		out[Lower(name)] = std::move(m);
		off += 8 + size;
	}
}

std::vector<Instance> LoadInstances(const std::string &gameData)
{
	std::vector<Instance> out;
	std::ifstream dat(gameData + "/data/gta_vc.dat");
	std::string line;
	while (std::getline(dat, line)) {
		if (line.rfind("IPL ", 0) != 0)
			continue;
		std::string rel = line.substr(4);
		rel.erase(rel.find_last_not_of(" \r\n\t") + 1);
		std::replace(rel.begin(), rel.end(), '\\', '/');
		std::ifstream ipl(gameData + "/" + Lower(rel));
		bool inInst = false;
		std::string l;
		while (std::getline(ipl, l)) {
			l.erase(l.find_last_not_of(" \r\n\t") + 1);
			if (l == "inst") {
				inInst = true;
				continue;
			}
			if (l == "end") {
				inInst = false;
				continue;
			}
			if (!inInst || l.empty() || l[0] == '#')
				continue;
			std::replace(l.begin(), l.end(), ',', ' ');
			std::istringstream ss(l);
			Instance in;
			int id;
			float sx, sy, sz;
			if (ss >> id >> in.model >> in.interior >> in.pos.x >> in.pos.y >> in.pos.z >> sx >> sy >> sz >> in.qx >>
			    in.qy >> in.qz >> in.qw) {
				in.model = Lower(in.model);
				out.push_back(in);
			}
		}
	}
	return out;
}

// "path" groups: a header "<type>, <id>" (0 ped, 1 car, 2 boat) followed by 12
// nodes "nodeType, next, cross, x, y, z, median, leftLanes, rightLanes, ...";
// coordinates are in 1/16 units, the median in world units (the game stores it
// x8 in CPathNode::width); nodeType 0 marks an unused slot.
std::vector<RoadSegment> LoadCarPaths(const std::string &gameData)
{
	constexpr float kLaneWidth = 5.0f;
	std::vector<RoadSegment> out;
	std::ifstream ipl(gameData + "/data/maps/paths.ipl");
	std::string line;
	bool inPath = false;
	int groupType = -1;
	struct Node {
		int type, next;
		Vec3f pos;
		float width;
	};
	std::vector<Node> group;
	auto flush = [&] {
		if (groupType == 1)
			for (const Node &n : group)
				if (n.type > 0 && n.next >= 0 && n.next < int(group.size()) && group[size_t(n.next)].type > 0)
					out.push_back({n.pos, group[size_t(n.next)].pos, n.width});
		group.clear();
	};
	while (std::getline(ipl, line)) {
		line.erase(line.find_last_not_of(" \r\n\t") + 1);
		if (line == "path") {
			inPath = true;
			continue;
		}
		if (!inPath)
			continue;
		if (line == "end")
			break;
		std::replace(line.begin(), line.end(), ',', ' ');
		std::istringstream ss(line);
		if (!line.empty() && line[0] != '\t' && line[0] != ' ') {
			flush();
			int id;
			ss >> groupType >> id;
			continue;
		}
		Node n{};
		int cross;
		float median, left, right;
		if (ss >> n.type >> n.next >> cross >> n.pos.x >> n.pos.y >> n.pos.z >> median >> left >> right) {
			n.pos = {n.pos.x / 16.0f, n.pos.y / 16.0f, n.pos.z / 16.0f};
			n.width = (left + right) * kLaneWidth + median;
			group.push_back(n);
		}
	}
	flush();
	return out;
}

bool WritePng(const std::string &path, int w, int h, const std::vector<uint8_t> &rgba)
{
	std::vector<uint8_t> raw;
	raw.reserve(size_t(h) * (w * 4 + 1));
	for (int y = 0; y < h; y++) {
		raw.push_back(0);
		raw.insert(raw.end(), rgba.begin() + size_t(y) * w * 4, rgba.begin() + size_t(y + 1) * w * 4);
	}
	uLongf zlen = compressBound(uLong(raw.size()));
	std::vector<uint8_t> z(zlen);
	compress2(z.data(), &zlen, raw.data(), uLong(raw.size()), 6);
	z.resize(zlen);
	auto be32 = [](std::vector<uint8_t> &v, uint32_t x) {
		for (int i = 3; i >= 0; i--)
			v.push_back(uint8_t(x >> (8 * i)));
	};
	auto chunk = [&](std::vector<uint8_t> &file, const char *tag, const std::vector<uint8_t> &body) {
		be32(file, uint32_t(body.size()));
		std::vector<uint8_t> tb(tag, tag + 4);
		tb.insert(tb.end(), body.begin(), body.end());
		file.insert(file.end(), tb.begin(), tb.end());
		be32(file, uint32_t(crc32(0, tb.data(), uInt(tb.size()))));
	};
	std::vector<uint8_t> file = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
	std::vector<uint8_t> ihdr;
	be32(ihdr, uint32_t(w));
	be32(ihdr, uint32_t(h));
	ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
	chunk(file, "IHDR", ihdr);
	chunk(file, "IDAT", z);
	chunk(file, "IEND", {});
	std::ofstream f(path, std::ios::binary);
	f.write(reinterpret_cast<const char *>(file.data()), std::streamsize(file.size()));
	return bool(f);
}
