#pragma once
#include <cstdint>
extern "C" volatile uint64_t *VCSourceGate(const char *name);
inline bool SourceSkip(const char *name){auto gate=VCSourceGate(name);return gate&&*gate;}
