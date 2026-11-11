#pragma once
#include "common.h"
#include <vector>
namespace labdraw {
struct Face {CVector p[3];rw::RGBA colour;};
rw::Atomic *Mesh(const std::vector<Face>&);
void Destroy(rw::Atomic *);
void Text(rw::Charset *,const char *,float,float,float,rw::RGBA);
bool Capture(const char *,int,int);
}
