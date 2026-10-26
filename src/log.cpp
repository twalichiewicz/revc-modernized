#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <string>

static FILE *gLog;

// The sessions before this one are kept (name.1.log the last, to name.3.log):
// the one that crashed or froze isn't gone with the next launch.
void LogOpen(const char *path)
{
	const std::string base = path;
	const size_t dot = base.rfind(".log");
	auto old = [&](int n) {
		return dot == std::string::npos ? base + "." + std::to_string(n)
		                                : base.substr(0, dot) + "." + std::to_string(n) + ".log";
	};
	constexpr int kKept = 3;
	std::remove(old(kKept).c_str());
	for (int n = kKept - 1; n >= 1; n--)
		std::rename(old(n).c_str(), old(n + 1).c_str());
	std::rename(path, old(1).c_str());
	gLog = fopen(path, "w");
}

void Log(const char *fmt, ...)
{
	FILE *out = gLog ? gLog : stderr;
	char stamp[32];
	time_t now = time(nullptr);
	strftime(stamp, sizeof stamp, "%H:%M:%S", localtime(&now));
	fprintf(out, "[%s] ", stamp);
	va_list ap;
	va_start(ap, fmt);
	vfprintf(out, fmt, ap);
	va_end(ap);
	fputc('\n', out);
	fflush(out);
}
