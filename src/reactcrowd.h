#pragma once
#include "reactphysics.h"
#include <cstddef>
namespace reactcrowd {
constexpr size_t Capacity = 12;
struct Entry { reactphysics::Body *body = nullptr; void *context = nullptr; };
struct Stats { unsigned bodies=0, pairs=0, contacts=0, iterations=0, blocked=0, escapes=0, escapeSearches=0, escapeTrials=0; };
// Entries are in stable generation order and remain alive during this call.
// The caller validates native ownership/generations and supplies only root owners.
// At most the first Capacity slots are visited; inactive, balanced, nonfinite
// and duplicate bodies are ignored. A null Sweep is the isolated no-world seam.
Stats Solve(const Entry *entries, size_t count, reactphysics::Sweep sweep);
}
