#include "hinttext.h"
#include "bindings.h"

#include <algorithm>

namespace hinttext {
namespace {

struct Rewritten {
	const char *key;
	uint16_t needs; // bindings::Switch
	const char *text;
};

// The phone is answered on the D-pad, and a drive-by is aimed (the look buttons are the pedals).
constexpr Rewritten kRewrites[] = {
    {"ANSWER", bindings::kPhone, "Press ~h~~k~~MOD_ANSWER_CALL~~w~ to answer your cell phone."},
    {"HELP31", bindings::kDriveBy, "To do a drive-by, hold ~h~~k~~MOD_DRIVEBY_AIM~~w~ and aim."},
    {"HELP32", bindings::kDriveBy, "Then fire using ~h~~k~~MOD_DRIVEBY_FIRE~~w~."},
    {"HELP33", bindings::kDriveBy, "Then fire using ~h~~k~~MOD_DRIVEBY_FIRE~~w~."},
};

} // namespace

void Expand(uint16_t *text, const Resolver &resolve)
{
	std::u16string in;
	for (size_t i = 0; i < kCapacity && text[i]; i++)
		in += char16_t(text[i]);
	std::u16string out;
	for (size_t i = 0; i < in.size();) {
		const size_t end = in.compare(i, 4, u"~k~~") == 0 ? in.find(u'~', i + 4) : std::u16string::npos;
		if (end == std::u16string::npos) { // not a key token, or one cut short: as it is
			out += in[i++];
			continue;
		}
		std::string name;
		for (size_t j = i + 4; j < end; j++)
			name += in[j] < 128 ? char(in[j]) : '?';
		out += resolve(name);
		i = end + 1;
	}
	size_t n = std::min(out.size(), kCapacity - 1);
	// Cut short mid-token, it goes: the font's parser runs on to a closing '~'.
	size_t tildes = 0, last = 0;
	for (size_t i = 0; i < n; i++)
		if (out[i] == u'~') {
			tildes++;
			last = i;
		}
	if (tildes % 2)
		n = last;
	out.resize(n);
	while (!out.empty() && out.back() == u'~') { // ends in a token: a space after it, or it goes if it's full
		if (out.size() < kCapacity - 1) {
			out += u' ';
			break;
		}
		out.resize(out.rfind(u'~', out.size() - 2)); // from the token's opening '~'
	}
	n = out.size();
	for (size_t i = 0; i < n; i++)
		text[i] = uint16_t(out[i]);
	text[n] = 0;
}

std::u16string Printable(std::u16string text)
{
	if (!text.empty() && text.back() == u'~')
		text += u' ';
	return text;
}

const uint16_t *PastToken(const uint16_t *s)
{
	const uint16_t *p = s + 1;
	while (*p && *p != '~')
		p++;
	return *p ? p + 1 : nullptr;
}

const char *Rewrite(std::string_view key, uint16_t switches)
{
	for (const Rewritten &r : kRewrites)
		if (key == r.key && (switches & r.needs) == r.needs)
			return r.text;
	return nullptr;
}

} // namespace hinttext
