#include "menulabels.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>
namespace {
using Pixels = std::array<uint8_t, 8>;
class Panels : public Canvas {
  public:
	explicit Panels(Pixels &p) : pixels(p) {}
	void SetTexture(void *) override {}
	void SetDepthMode(DepthMode) override {}
	void Tri(const Vtx &, const Vtx &, const Vtx &, CRGBA, CRGBA, CRGBA) override {}
	void Flush() override
	{
		if (pending) {
			pixels.fill(40);
			pixels[0] = 255;
			pending = false;
		}
	}
	bool pending = true;

  private:
	Pixels &pixels;
};
class NativeFont {
  public:
	explicit NativeFont(Pixels &p) : pixels(p) {}
	void Emit(const menulabels::Label &label)
	{
		// The real CFont MakeRoom draws an earlier batch before queuing more.
		if (pending.size() == 3)
			Flush();
		pending.push_back({int(label.centerY), label.style.colour.r});
	}
	void Flush()
	{
		for (auto p : pending)
			pixels[size_t(p.first)] = p.second;
		pending.clear();
	}

  private:
	Pixels &pixels;
	std::vector<std::pair<int, uint8_t>> pending;
};
} // namespace
int main()
{
	Pixels pixels{};
	Panels panels(pixels);
	NativeFont font(pixels);
	menulabels::Batch labels;
	for (int i = 0; i < 8; i++)
		labels.Add(0, float(i), i == 0 ? "SELECTED TAB" : "SETTING", {1, 1, 1, {uint8_t(i == 0 ? 0 : 255), 0, 0, 255}});
	labels.AfterGeometry(panels, [&](const menulabels::Label &label) { font.Emit(label); });
	font.Flush();
	assert(pixels[0] == 0); // black selected-tab letters remain on the white pill
	for (int i = 1; i < 8; i++)
		assert(pixels[size_t(i)] == 255); // every label stays above its dark panel
	puts("all menu geometry-before-font tests passed");
}
