#include "traffic.h"
#include <cstdio>

static int failures;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n", __LINE__, #c); failures++; } } while (0)

int main()
{
	// Missing population interception leaves every native generation call owned by the game.
	auto hooks = traffic::InstallHooks(true, [] { return true; }, [] { return false; });
	CHECK(hooks.cars && !hooks.population);
	CHECK(!traffic::PopulationMayRun(hooks, true, true));

	// A missing car hook must still attempt and own population interception independently.
	bool populationAttempted = false;
	hooks = traffic::InstallHooks(true, [] { return false; }, [&] { populationAttempted = true; return true; });
	CHECK(populationAttempted && !hooks.cars && hooks.population);
	CHECK(traffic::PopulationMayRun(hooks, true, true));
	CHECK(!traffic::PopulationMayRun(hooks, false, true));
	CHECK(!traffic::PopulationMayRun(hooks, true, false));

	// The car-model pool prerequisite cannot prevent installing the unrelated population hook.
	bool carAttempted = false;
	populationAttempted = false;
	hooks = traffic::InstallHooks(false, [&] { carAttempted = true; return true; },
	    [&] { populationAttempted = true; return true; });
	CHECK(!carAttempted && populationAttempted && !hooks.cars && hooks.population);
	CHECK(traffic::PopulationMayRun(hooks, true, true));

	// If both interception attempts fail, resolved population symbols cannot start virtual agents.
	populationAttempted = false;
	hooks = traffic::InstallHooks(true, [] { return false; }, [&] { populationAttempted = true; return false; });
	CHECK(populationAttempted && !hooks.cars && !hooks.population);
	CHECK(!traffic::PopulationMayRun(hooks, true, true));
	CHECK(!traffic::PopulationMayRun({}, true, true));
	if (!failures) std::puts("trafficinstall: all passed");
	return failures ? 1 : 0;
}
