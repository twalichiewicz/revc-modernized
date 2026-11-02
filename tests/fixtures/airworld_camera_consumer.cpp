// Reuse the existing camera's dependency fixture in a separate translation
// unit, while executing its ACTUAL SetupEntityVisibility callback below.
// Rename fixture-owned globals/hooks so the airworld consumer supplies its
// own real hook registry and game API without duplicate test definitions.
#define main AirworldUnusedCameraChecks
#define gConfig gJointCameraConfig
#define Log JointCameraLog
#define hooks jointcamera_hooks
#define planeflight jointcamera_planeflight
#include "../test_aircraftcamerahook.cpp"
#undef planeflight
#undef hooks
#undef Log
#undef gConfig
#undef main

int AirworldActualCameraVisibility(uint8_t *entity,uint8_t *cockpit,int (*next)(uint8_t *))
{
 camera::gCockpit=cockpit;
 camera::orig_SetupEntityVisibility=next;
 return camera::Hook_SetupEntityVisibility(entity);
}
