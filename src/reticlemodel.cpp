#include "reticlemodel.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reticlemodel {
Settings Sanitize(Settings s)
{
	s.style = std::clamp(s.style, 0, kStyles - 1);
	s.color = std::clamp(s.color, 0, kColors - 1);
	s.size = std::isfinite(s.size) ? std::clamp(s.size, 0.5f, 2.5f) : 1;
	s.thickness = std::isfinite(s.thickness) ? std::clamp(s.thickness, 0.5f, 3.0f) : 1;
	return s;
}
Shape Build(Settings settings, float cx, float cy, float h, float spread, float tanHalf)
{
	const Settings s = Sanitize(settings);
	Shape out;
	out.edge = fpcross::Edge(h);
	const float t = std::max(1.0f, std::round(fpcross::Thickness(h) * s.thickness));
	const float length = std::max(1.0f, std::round(fpcross::Length(h) * s.size));
	const float gap =
	    fpcross::MinGap(h) * s.size +
	    (s.dynamic ? std::max(0.0f, fpcross::Gap(std::clamp(spread, 0.0f, 0.3f), tanHalf, h) - fpcross::MinGap(h)) : 0);
	const float x = std::round(cx - t / 2), y = std::round(cy - t / 2), mx = x + t / 2, my = y + t / 2;
	if (s.style != 1) {
		if (s.style == 0)
			out.rects[out.count++] = {x, std::floor(my - gap) - length, t, length};
		out.rects[out.count++] = {x, std::ceil(my + gap), t, length};
		out.rects[out.count++] = {std::floor(mx - gap) - length, y, length, t};
		out.rects[out.count++] = {std::ceil(mx + gap), y, length, t};
	}
	if (s.dot || s.style == 1)
		out.rects[out.count++] = {x, y, t, t};
	return out;
}
void Change(Settings &s, Row row, int direction)
{
	s = Sanitize(s);
	const int step = direction < 0 ? -1 : 1;
	switch (row) {
	case Row::kStyle:
		s.style = (s.style + step + kStyles) % kStyles;
		break;
	case Row::kColor:
		s.color = (s.color + step + kColors) % kColors;
		break;
	case Row::kSize:
		s.size = std::clamp(s.size + 0.1f * step, 0.5f, 2.5f);
		break;
	case Row::kThickness:
		s.thickness = std::clamp(s.thickness + 0.25f * step, 0.5f, 3.0f);
		break;
	case Row::kDot:
		s.dot = !s.dot;
		break;
	case Row::kDynamic:
		s.dynamic = !s.dynamic;
		break;
	case Row::kDefaults:
		s = {};
		break;
	case Row::kCount:
		break;
	}
}
std::array<Value, 6> Values(Settings s)
{
	s = Sanitize(s);
	char size[16], thickness[16];
	snprintf(size, sizeof size, "%.2f", s.size);
	snprintf(thickness, sizeof thickness, "%.2f", s.thickness);
	return {{{"Style", std::to_string(s.style)},
	         {"Color", std::to_string(s.color)},
	         {"Size", size},
	         {"Thickness", thickness},
	         {"CenterDot", s.dot ? "1" : "0"},
	         {"DynamicSpread", s.dynamic ? "1" : "0"}}};
}
CRGBA Color(int i)
{
	static constexpr CRGBA colors[] = {{255, 255, 255, 230}, {100, 255, 120, 230}, {90, 220, 255, 230},
	                                   {255, 220, 80, 230},  {255, 110, 190, 230}, {255, 100, 90, 230}};
	return colors[std::clamp(i, 0, kColors - 1)];
}
const char *StyleName(int i)
{
	static const char *names[] = {"CROSS", "DOT", "T-SHAPE"};
	return names[std::clamp(i, 0, kStyles - 1)];
}
const char *ColorName(int i)
{
	static const char *names[] = {"WHITE", "GREEN", "CYAN", "YELLOW", "PINK", "RED"};
	return names[std::clamp(i, 0, kColors - 1)];
}
std::string Label(Settings s, Row row)
{
	s = Sanitize(s);
	char value[24];
	switch (row) {
	case Row::kStyle:
		return StyleName(s.style);
	case Row::kColor:
		return ColorName(s.color);
	case Row::kSize:
		snprintf(value, sizeof value, "%.1f", s.size);
		return value;
	case Row::kThickness:
		snprintf(value, sizeof value, "%.2f", s.thickness);
		return value;
	case Row::kDot:
		return s.dot ? "ON" : "OFF";
	case Row::kDynamic:
		return s.dynamic ? "ON" : "OFF";
	case Row::kDefaults:
		return "RESTORE";
	case Row::kCount:
		return {};
	}
	return {};
}
} // namespace reticlemodel
