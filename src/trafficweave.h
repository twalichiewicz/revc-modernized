#pragma once
// Native traffic avoidance uses exact float equality to finish an outer scan.
// An unordered candidate leaves that scan running forever, including for boats.
namespace trafficweave {
bool Install();
}
