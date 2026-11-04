#pragma once
#include <cstdint>
namespace partsmodel {
constexpr int kParts=8;
constexpr uint32_t kQuietMs=350;
struct Part {const char *name;bool panel;int index;};
int Slot(int component); // tComponent 5..10, 16..17; -1 otherwise
const Part &Describe(int slot); // 0..kParts-1
float Scale(int state);        // intact, damaged, swinging/damaged2, missing
struct Episode {uint32_t last=0;bool seen=false,advanced=false;};
// Every attempt keeps a continuous contact alive. An unsuccessful small hit
// leaves the episode's one progression available for a later stronger hit.
bool Contact(Episode &,uint32_t now);
void Commit(Episode &);
}
