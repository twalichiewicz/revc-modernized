#include "vmuse.h"

#include <algorithm>

namespace vm {
namespace {

constexpr int32_t kStateFall = 0x2a, kStateGetUp = 0x2b, kStateDie = 0x36, kStateDead = 0x37;

} // namespace

Use Choose(const Moment &m)
{
	if (!m.firstPersonOnFoot)
		return Use::kNone;
	const bool down = m.pedState == kStateFall || (m.pedState == kStateGetUp && !m.dodging) || m.pedState == kStateDie ||
	                  m.pedState == kStateDead;
	if (!m.enabled || !m.covered || !m.armsOk || down || m.swimming || m.climbing)
		return Use::kOld;
	return Use::kNew;
}

bool SameModel(const ModelKey &a, const ModelKey &b)
{
	return a.geometry && a.geometry == b.geometry && a.texture == b.texture && a.model == b.model &&
	       a.vertices == b.vertices && a.triangles == b.triangles;
}

bool Heartbeat::Beat(bool taken)
{
	missed_ = taken ? 0 : missed_ + 1;
	if (missed_ >= 3)
		down_ = true;
	return !down_;
}

bool ShotSeen::Update(int weapon, int clip, uint32_t frame)
{
	const bool shot = weapon == weapon_ && frame == frame_ + 1 && clip < clip_;
	weapon_ = weapon;
	clip_ = clip;
	frame_ = frame;
	return shot;
}

float ReloadClock::Progress(bool reloading, uint32_t now, uint32_t end)
{
	if (!reloading) {
		running_ = false;
		return 0.0f;
	}
	if (!running_ || end != end_) { // a new reload: from now
		start_ = now;
		end_ = end;
		running_ = true;
	}
	const float length = float(int32_t(end_ - start_));
	return length > 0.0f ? std::clamp(float(int32_t(now - start_)) / length, 0.0f, 1.0f) : 1.0f;
}

} // namespace vm
