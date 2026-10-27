// Unit tests for the font buffer's room check (CFont queues text in 1 KB and never checks it fits): make test
#include "fontbuf.h"

#include <cstdio>
#include <cstring>
#include <vector>

static int gFailures;

#define CHECK(cond)                                                                                                    \
	do {                                                                                                               \
		if (!(cond)) {                                                                                                 \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
			gFailures++;                                                                                               \
		}                                                                                                              \
	} while (0)

static void TestEmptyBufferTakesAPrompt()
{
	CHECK(fontbuf::Fits(0, 5, 1)); // "GREET"
	CHECK(fontbuf::Fits(0, 4, 1)); // a button's token
}

static void TestNearlyFullBufferDoesNot()
{
	CHECK(!fontbuf::Fits(1000, 5, 1));
	CHECK(!fontbuf::Fits(fontbuf::kBytes, 1, 1));
}

// Each word may be queued as its own entry (a 48-byte header), and a drop shadow queues it all again.
static void TestHeadersAndShadowCount()
{
	const size_t one = fontbuf::Bytes(5, 1);
	CHECK(one >= 2 * (fontbuf::kHeader + 6 * 2)); // header and five units plus a terminator, twice
	CHECK(fontbuf::Bytes(7, 2) > fontbuf::Bytes(7, 1)); // "PICK UP": two words, two headers
	CHECK(fontbuf::Fits(fontbuf::kBytes - one, 5, 1));
	CHECK(!fontbuf::Fits(fontbuf::kBytes - one + 4, 5, 1));
}

// A buffer as CFont::PrintString fills it: a 48-byte header, the text, a terminator, padded to 4.
static void Queue(std::vector<uint8_t> &buf, const std::vector<uint16_t> &text)
{
	buf.insert(buf.end(), fontbuf::kHeader, uint8_t(0xab));
	for (uint16_t c : text) {
		buf.push_back(uint8_t(c));
		buf.push_back(uint8_t(c >> 8));
	}
	buf.push_back(0);
	buf.push_back(0);
	while (buf.size() % 4)
		buf.push_back(0);
}

static void TestGoodBufferChecksOut()
{
	std::vector<uint8_t> buf;
	Queue(buf, {'G', 'R', 'E', 'E', 'T'});
	Queue(buf, {'~', 'w', '~', 'O', 'K'});
	CHECK(fontbuf::GoodUpTo(buf.data(), buf.size()) == buf.size());
	CHECK(fontbuf::GoodUpTo(buf.data(), 0) == 0);
}

// What crashed the game: a character no font has (0x9979, 0xffe0) or text
// that runs on unterminated; the buffer is good only up to that entry.
static void TestBadEntryIsFound()
{
	std::vector<uint8_t> buf;
	Queue(buf, {'G', 'R', 'E', 'E', 'T'});
	const size_t first = buf.size();
	Queue(buf, {'O', 0x9979, 'K'});
	Queue(buf, {'L', 'A', 'T', 'E', 'R'});
	CHECK(fontbuf::GoodUpTo(buf.data(), buf.size()) == first);
	std::vector<uint8_t> runOn;
	Queue(runOn, {'A'});
	const size_t ok = runOn.size();
	runOn.insert(runOn.end(), fontbuf::kHeader, uint8_t(0));
	for (int i = 0; i < 6; i++)
		runOn.push_back('B'); // no terminator before the end
	CHECK(fontbuf::GoodUpTo(runOn.data(), runOn.size()) == ok);
}

// The font's parser runs on from an unclosed '~' to the next one anywhere in
// memory, copying all of it: our text never leaves one open.
static void TestUnclosedTokenIsCut()
{
	uint16_t a[] = {'P', 'R', 'E', 'S', 'S', ' ', '~', '@', 'u', 0};
	fontbuf::CloseTokens(a);
	CHECK(a[5] == ' ' && a[6] == 0);
	uint16_t b[] = {'~', 'w', '~', 'O', 'K', 0};
	fontbuf::CloseTokens(b);
	CHECK(b[3] == 'O' && b[5] == 0);
	uint16_t c[] = {'~', 0};
	fontbuf::CloseTokens(c);
	CHECK(c[0] == 0);
}

// What still crashed it: an entry ending in a token (an interaction prompt's
// button: CFont::PrintString had dropped the space after it). The renderer
// steps past its terminator into the next entry's header, read as text.
static void TestEntryEndingInATokenIsBad()
{
	std::vector<uint8_t> buf;
	Queue(buf, {'G', 'R', 'E', 'E', 'T'});
	const size_t first = buf.size();
	Queue(buf, {'~', '@', 'u', '~'});
	Queue(buf, {'L', 'A', 'T', 'E', 'R'});
	CHECK(fontbuf::GoodUpTo(buf.data(), buf.size()) == first);
	std::vector<uint8_t> spaced;
	Queue(spaced, {'~', '@', 'u', '~', ' '});
	Queue(spaced, {'~', 'w', '~', 'O', 'K'});
	CHECK(fontbuf::GoodUpTo(spaced.data(), spaced.size()) == spaced.size());
}

// Our text never ends in a token: two spaces after it (the font drops one).
static void TestEndAfterToken()
{
	uint16_t a[8] = {'~', '@', 'u', '~', 0};
	fontbuf::EndAfterToken(a, 8);
	CHECK(a[4] == ' ' && a[5] == ' ' && a[6] == 0);
	uint16_t b[8] = {'~', '@', 'u', '~', ' ', 0};
	fontbuf::EndAfterToken(b, 8);
	CHECK(b[4] == ' ' && b[5] == ' ' && b[6] == 0);
	uint16_t c[8] = {'~', 'w', '~', 'O', 'K', 0};
	fontbuf::EndAfterToken(c, 8);
	CHECK(c[4] == 'K' && c[5] == 0);
	uint16_t full[6] = {'A', '~', '@', 'u', '~', 0}; // no room for the spaces: the token goes
	fontbuf::EndAfterToken(full, 6);
	CHECK(full[0] == 'A' && full[1] == 0);
}

int main()
{
	TestEntryEndingInATokenIsBad();
	TestEndAfterToken();
	TestGoodBufferChecksOut();
	TestBadEntryIsFound();
	TestUnclosedTokenIsCut();
	TestEmptyBufferTakesAPrompt();
	TestNearlyFullBufferDoesNot();
	TestHeadersAndShadowCount();
	if (gFailures) {
		printf("%d font buffer test(s) failed\n", gFailures);
		return 1;
	}
	printf("all font buffer tests passed\n");
	return 0;
}
