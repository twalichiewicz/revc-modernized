#pragma once

// Appends a line to ~/Library/Application Support/macVC/vcmodernhud.log.
void Log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
// Starts this session's log, keeping the last few (vcmodernhud.1.log the last).
void LogOpen(const char *path);
