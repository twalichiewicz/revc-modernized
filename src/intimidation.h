#pragma once
namespace intimidation {
enum class Response { None, Brake, Flee, Surrender, Fight, Ram };
struct Threat {
	bool aiming = false, visible = false, targeted = false, mission = false, law = false, sameCar = false;
	float distance = 0, speed = 0;
	int temper = 50;
	int fear = 45;
	bool armed = false, gang = false, onFoot = false, targetInCar = false;
};
class Driver {
  public:
	Response Update(const Threat &threat, float dt);

  private:
	float watched_ = 0, absent_ = 0;
	Response committed_ = Response::None;
};
} // namespace intimidation
