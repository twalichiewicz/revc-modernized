CXX      := xcrun clang++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -arch arm64 -mmacosx-version-min=15.0
BUILD    := build

DYLIB   := $(BUILD)/libVCModernHUD.dylib
BAKE    := $(BUILD)/vcmm-bake
PREVIEW := $(BUILD)/vcmm-preview

SHARED := src/brickphone.cpp src/canvas.cpp src/mapdata.cpp src/projection.cpp src/scene.cpp
PLUGIN := $(SHARED) src/aimcam.cpp src/config.cpp src/controls.cpp src/draw.cpp src/driveby.cpp src/game.cpp src/gps.cpp src/hooks.cpp src/hud.cpp src/log.cpp src/menu.cpp src/minimap.cpp src/phone.cpp src/radiologos.cpp src/text.cpp src/wheel.cpp
GAMEFILES := tools/bake/gamefiles.cpp

all: $(DYLIB) $(BAKE)

$(DYLIB): $(PLUGIN) $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -fvisibility=hidden -dynamiclib \
		-install_name @executable_path/../Frameworks/libVCModernHUD.dylib -o $@ $(PLUGIN)

$(BAKE): tools/bake/bake.cpp $(GAMEFILES) src/mapdata.cpp src/radiologos.cpp $(wildcard tools/bake/*.h src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ tools/bake/bake.cpp $(GAMEFILES) src/mapdata.cpp src/radiologos.cpp -lz

$(PREVIEW): tools/preview/preview.cpp $(SHARED) $(GAMEFILES) $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tools/preview/preview.cpp $(SHARED) $(GAMEFILES) -lz

$(BUILD)/test_projection: tests/test_projection.cpp src/projection.cpp src/projection.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_projection.cpp src/projection.cpp

test: $(BUILD)/test_projection
	$(BUILD)/test_projection

preview: $(PREVIEW)

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)/*.dylib $(BUILD)/vcmm-* $(BUILD)/test_*

.PHONY: all test preview clean
