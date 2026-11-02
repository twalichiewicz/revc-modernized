#pragma once
#include "canvas.h"
#include "text.h"
#include <string>
#include <vector>

namespace menulabels {
struct Label {
	float x, centerY;
	std::string value;
	text::Style style;
};
// CFont may draw queued text early when its buffer fills. Delay submitting
// page labels until every panel, image and highlight has reached the GPU.
class Batch {
  public:
	void Clear() { labels_.clear(); }
	void Add(float x, float centerY, const std::string &value, const text::Style &style)
	{
		labels_.push_back({x, centerY, value, style});
	}
	template <typename Emit> void AfterGeometry(Canvas &canvas, Emit emit)
	{
		canvas.Flush();
		for (const Label &label : labels_)
			emit(label);
		labels_.clear();
	}

  private:
	std::vector<Label> labels_;
};
} // namespace menulabels
