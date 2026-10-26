CXX      := xcrun clang++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -arch arm64 -mmacosx-version-min=15.0
BUILD    := build

DYLIB   := $(BUILD)/libVCModernHUD.dylib
BAKE    := $(BUILD)/vcmm-bake
PREVIEW := $(BUILD)/vcmm-preview

SHARED := src/brickphone.cpp src/canvas.cpp src/mapdata.cpp src/projection.cpp src/scene.cpp
PLUGIN := $(SHARED) src/aimcam.cpp src/alert.cpp src/anims.cpp src/body.cpp src/camera.cpp src/carlights.cpp src/carradio.cpp src/climb.cpp src/clouds.cpp src/combat.cpp src/config.cpp src/controls.cpp src/credit.cpp src/crouch.cpp src/shapes.cpp src/shootdodge.cpp src/sight.cpp src/draw.cpp src/driveby.cpp src/game.cpp src/grade.cpp src/gps.cpp src/heat.cpp src/heli.cpp src/hooks.cpp src/hunter.cpp src/hud.cpp src/interact.cpp src/log.cpp src/menu.cpp src/minimap.cpp src/phone.cpp src/places.cpp src/player.cpp src/police.cpp src/prompt.cpp src/radiohud.cpp src/radiologos.cpp src/rtshadows.cpp src/rumble.mm src/shaders.cpp src/sky.cpp src/steering.cpp src/story.cpp src/swim.cpp src/text.cpp src/traffic.cpp src/turns.cpp src/viewmodel.cpp src/wanted.cpp src/wantedhud.cpp src/watch.cpp src/witness.cpp src/wheel.cpp
GAMEFILES := tools/bake/gamefiles.cpp

all: $(DYLIB) $(BAKE)

$(DYLIB): $(PLUGIN) $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -fvisibility=hidden -fobjc-arc -dynamiclib \
		-install_name @executable_path/../Frameworks/libVCModernHUD.dylib -o $@ $(PLUGIN) \
		-framework Foundation -framework GameController -framework CoreHaptics

$(BAKE): tools/bake/bake.cpp $(GAMEFILES) src/mapdata.cpp src/radiologos.cpp $(wildcard tools/bake/*.h src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ tools/bake/bake.cpp $(GAMEFILES) src/mapdata.cpp src/radiologos.cpp -lz

$(PREVIEW): tools/preview/preview.cpp $(SHARED) $(GAMEFILES) $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tools/preview/preview.cpp $(SHARED) $(GAMEFILES) -lz

$(BUILD)/test_projection: tests/test_projection.cpp src/projection.cpp src/projection.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_projection.cpp src/projection.cpp

$(BUILD)/test_turns: tests/test_turns.cpp src/turns.cpp src/turns.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_turns.cpp src/turns.cpp

$(BUILD)/test_heat: tests/test_heat.cpp src/heat.cpp src/heat.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_heat.cpp src/heat.cpp

test: $(BUILD)/test_projection $(BUILD)/test_turns $(BUILD)/test_heat
	$(BUILD)/test_projection
	$(BUILD)/test_turns
	$(BUILD)/test_heat

preview: $(PREVIEW)

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)/*.dylib $(BUILD)/vcmm-* $(BUILD)/test_*

.PHONY: all test preview clean
