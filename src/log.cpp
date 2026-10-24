#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>

static FILE *gLog;

void LogOpen(const char *path)
{
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
