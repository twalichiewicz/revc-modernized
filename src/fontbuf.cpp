#include "fontbuf.h"

namespace fontbuf {

size_t Bytes(size_t units, size_t words)
{
	const size_t text = ((units + words) * 2 + 3) & ~size_t(3); // a terminator per piece, 4-aligned
	return 2 * (words * kHeader + text);
}

bool Fits(size_t used, size_t units, size_t words) { return used <= kBytes && Bytes(units, words) <= kBytes - used; }

size_t GoodUpTo(const uint8_t *buffer, size_t used)
{
	size_t good = 0;
	while (good < used) {
		size_t p = good + kHeader;
		bool ended = false;
		uint16_t last = 0;
		for (; p + 2 <= used; p += 2) {
			const uint16_t c = uint16_t(buffer[p] | buffer[p + 1] << 8);
			if (c == 0) {
				if (last == '~') // a token's end, then the terminator: the renderer steps past it
					return good;
				ended = true;
				p += 2;
				break;
			}
			if (c >= kFirstNotText)
				return good;
			last = c;
		}
		if (!ended)
			return good;
		good = (p + 3) & ~size_t(3);
	}
	return good < used ? good : used;
}

void EndAfterToken(uint16_t *text, size_t capacity)
{
	size_t n = 0;
	while (text[n])
		n++;
	size_t end = n;
	while (end > 0 && text[end - 1] == ' ')
		end--;
	if (end == 0 || text[end - 1] != '~')
		return;
	if (end + 2 < capacity) {
		text[end] = text[end + 1] = ' ';
		text[end + 2] = 0;
		return;
	}
	// No room: the token goes (from its opening '~').
	size_t open = end - 1;
	while (open > 0 && text[open - 1] != '~')
		open--;
	text[open > 0 ? open - 1 : 0] = 0;
}

void CloseTokens(uint16_t *text)
{
	uint16_t *open = nullptr;
	for (uint16_t *p = text; *p; p++)
		if (*p == '~')
			open = open ? nullptr : p;
	if (open)
		*open = 0;
}

} // namespace fontbuf
