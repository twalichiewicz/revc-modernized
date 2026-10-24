#pragma once

// Appends a line to ~/Library/Application Support/macVC/vcminimap.log.
void Log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void LogOpen(const char *path);
