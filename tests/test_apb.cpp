#include "heat.h"
#include <cstdio>

static int failures;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main()
{
	heat::Model m;
	const heat::Looks known{"player", 145, 1, 2, 42};
	const heat::Looks clean{"suit", 150, 3, 4, 99};
	m.CopSees(known, {}, 1000, 3);
	m.Tick({}, 5001);
	// An active search retains close-range recognition.
	CHECK(m.RecognitionRate(clean, 2, false) > 0);
	m.Tick({900, 0, 0}, 5002 + heat::kSearchMs[3]);
	CHECK(m.state() == heat::State::Apb);
	CHECK(m.RecognitionRate(clean, 0, false) == 0);
	CHECK(m.RecognitionRate(clean, 2, true) == 0);
	CHECK(m.RecognitionRate({"suit"}, 2, false) == 0);
	CHECK(m.RecognitionRate(known, 25, false) > 0);
	CHECK(m.RecognitionRate({"suit", 145, 1, 2}, 25, true) > 0);
	CHECK(m.description().outfit == "player" && m.escapedStars() == 3);
	// Directly witnessed crime still identifies a wholly clean description.
	m.CopSees(clean, {4, 0, 0}, 90000, 1);
	CHECK(m.state() == heat::State::Wanted && m.stars() == 3);
	CHECK(m.description().outfit == "suit");
	if (!failures) std::puts("apb: all passed");
	return failures ? 1 : 0;
}
