#include "rtshadows.h"
#include "hooks.h"
#include "log.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace rtshadows {
namespace {

constexpr double kEvery = 30.0; // s between lines of a kind after the first few
constexpr int kFirst = 3;

void (*orig_Debug)(const char *format, ...);

// One kind of line: the first few as they come, then one every kEvery
// seconds with how many there were in between.
struct Kind {
	const char *match;
	int seen = 0;
	int since = 0;
	double last = 0.0;
};
Kind gKinds[] = {{"trace #"}, {"acceleration structure built"}, {"MetalRT"}};

double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

void Note(char *line)
{
	if (!strstr(line, "MetalRT"))
		return;
	const char *text = strncmp(line, "[DBG]: ", 7) ? line : line + 7;
	if (char *end = strchr(line, '\n'))
		*end = '\0';
	for (Kind &kind : gKinds)
		if (strstr(text, kind.match)) {
			kind.seen++;
			const double now = Now();
			if (kind.seen <= kFirst || now - kind.last >= kEvery) {
				if (kind.since)
					Log("rt shadows: %s (%d more since the last)", text, kind.since);
				else
					Log("rt shadows: %s", text);
				kind.last = now;
				kind.since = 0;
			} else {
				kind.since++;
			}
			return;
		}
}

void Hook_Debug(const char *format, ...)
{
	char line[1024];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof line, format, args);
	va_end(args);
	orig_Debug("%s", line);
	Note(line);
}

} // namespace

bool Install() { return hooks::Install("_Z9re3_debugPKcz", Hook_Debug, orig_Debug); }

} // namespace rtshadows
