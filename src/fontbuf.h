#pragma once
// The game's font queues text in one 1 KB buffer (FontRenderStateBuf):
// CFont::PrintString writes a 48-byte header and the text for each piece it
// queues, and only flushes (CFont::RenderFontBuffer) once the buffer is
// already full, so a piece queued into a nearly full buffer runs past its
// end, over CFont::ButtonSprite and what follows. The game's own HUD never
// queues enough to; with the mod's text as well it can, which corrupted the
// buffer and crashed the next render. So the mod's text checks for room first.
//
// And PrintString copies a '~' token as far as CFont::ParseToken says it
// goes, which for one never closed is the next '~' anywhere in memory: a
// run of whatever's there, characters no font has, which crashed the render
// (EXC_BAD_ACCESS in CFont::RenderFontBuffer, reading a glyph's width for
// 0x9979 or 0xffe0). So the mod's text never leaves a token open, and the
// buffer is checked before it's drawn: a bad entry and what's after it go.
// Unit-tested (tests/test_fontbuf.cpp).
#include <cstddef>
#include <cstdint>

namespace fontbuf {

constexpr size_t kBytes = 0x400;  // FontRenderStateBuf
constexpr size_t kHeader = 0x30;  // CFontRenderState, before each piece

// The most a string of `units` UTF-16 units in `words` words can take: a
// header per word (a line or word may be queued on its own), the text with
// its terminator, rounded to 4, and all of it again for a drop shadow.
size_t Bytes(size_t units, size_t words);
// Whether that string still fits after `used` bytes.
bool Fits(size_t used, size_t units, size_t words);

constexpr uint16_t kFirstNotText = 0x100; // the game's text is 8-bit; past it, no font has a glyph
// How many of the buffer's first `used` bytes hold good entries: each a
// header, text the font can draw ending in a terminator (not straight after
// a token: EndAfterToken), padded to 4.
size_t GoodUpTo(const uint8_t *buffer, size_t used);
// Cuts `text` (terminated) at a '~' that's never closed.
void CloseTokens(uint16_t *text);
// Never ending in a token: CFont::RenderFontBuffer steps past the terminator
// after one (into the next entry's header, read as text: garbage glyphs and
// styles, the crash at an interaction prompt), and CFont::PrintString drops
// a single space at the end, so two go after it; the token goes if
// `capacity` (units, with the terminator) has no room for them.
void EndAfterToken(uint16_t *text, size_t capacity);

} // namespace fontbuf
