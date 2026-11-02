#include "intimidation.h"
#include <algorithm>
#include <cmath>
namespace intimidation {
Response Driver::Update(const Threat &t, float dt)
{
	dt = std::clamp(dt, 0.0f, 0.25f);
	if (t.mission || t.sameCar) {
		watched_ = absent_ = 0;
		committed_ = Response::None;
		return Response::None;
	}
	if (!t.aiming || !t.visible || !t.targeted || !std::isfinite(t.distance) || t.distance > 35.0f) {
		absent_ += dt;
		if (absent_ > 2.0f) {
			watched_ = 0;
			committed_ = Response::None;
		}
		return Response::None;
	}
	absent_ = 0;
	if (committed_ != Response::None)
		return committed_;
	watched_ += dt;
	if (watched_ < 0.35f)
		return Response::None;
	// Police confront a credible threat, including one inside another car.
	// Braking/surrender are civilian choices, never police choices.
	if (t.law)
		committed_ = !t.onFoot && t.targetInCar ? Response::Ram : Response::Fight;
	else if (!t.onFoot && t.speed > 3.0f && watched_ < 1.25f)
		return Response::Brake;
	else if (t.temper > t.fear + 15 && (t.armed || (t.onFoot && t.distance < 3.0f)))
		committed_ = Response::Fight;
	else if (t.fear >= 60 || t.temper >= 55 || t.distance > 12.0f || (!t.onFoot && t.speed > 3.0f))
		committed_ = Response::Flee;
	else
		committed_ = Response::Surrender;
	return committed_;
}
} // namespace intimidation
