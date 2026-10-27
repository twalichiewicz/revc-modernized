CXX      := xcrun clang++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -arch arm64 -mmacosx-version-min=15.0
BUILD    := build

DYLIB   := $(BUILD)/libVCModernHUD.dylib
BAKE    := $(BUILD)/vcmm-bake
PREVIEW := $(BUILD)/vcmm-preview
VMTOOL  := $(BUILD)/vcmm-viewmodel
VMCORE  := src/vmanim.cpp src/vmarms.cpp src/vmfit.cpp src/vmgun.cpp src/vmhands.cpp src/vmpose.cpp src/vmskin.cpp src/vmview.cpp src/gunassets.cpp $(BUILD)/gunassets.S

SHARED := src/brickphone.cpp src/canvas.cpp src/mapdata.cpp src/projection.cpp src/scene.cpp
PLUGIN := $(SHARED) src/actions.cpp src/aimcam.cpp src/aimslow.cpp src/alert.cpp src/anims.cpp src/bindings.cpp src/body.cpp src/camera.cpp src/carlights.cpp src/carradio.cpp src/climb.cpp src/clouds.cpp src/combat.cpp src/config.cpp src/controls.cpp src/controlsmodel.cpp src/controlspage.cpp src/credit.cpp src/crouch.cpp src/shapes.cpp src/shootdodge.cpp src/sight.cpp src/draw.cpp src/driveby.cpp src/game.cpp src/grade.cpp src/fontbuf.cpp src/fpbob.cpp src/fpcross.cpp src/fpfire.cpp src/fpgait.cpp src/fplook.cpp src/fpshoot.cpp src/fpmove.cpp src/fpwalk.cpp src/gps.cpp src/gunbank.cpp src/gunsfx.cpp src/heat.cpp src/heli.cpp src/hints.cpp src/hinttext.cpp src/hooks.cpp src/hunter.cpp src/hud.cpp src/inifile.cpp src/magnet.cpp src/interact.cpp src/keys.cpp src/log.cpp src/menu.cpp src/minimap.cpp src/phone.cpp src/places.cpp src/player.cpp src/poi.cpp src/police.cpp src/prompt.cpp src/radiohud.cpp src/radiologos.cpp src/rtshadows.cpp src/rumble.mm src/scope.cpp src/sfxdsp.cpp src/shaders.cpp src/shotsound.cpp src/shotsynth.cpp src/sniper.cpp src/sky.cpp src/steering.cpp src/story.cpp src/swim.cpp src/text.cpp src/traffic.cpp src/turns.cpp src/viewmodel.cpp src/vmanim.cpp src/vmarms.cpp src/vmfit.cpp src/vmgun.cpp src/vmhands.cpp src/vmpose.cpp src/vmskin.cpp src/vmuse.cpp src/vmview.cpp src/fpgun.cpp src/gunassets.cpp $(BUILD)/gunassets.S src/wanted.cpp src/wantedhud.cpp src/watch.cpp src/witness.cpp src/wheel.cpp
GAMEFILES := tools/bake/gamefiles.cpp
GUNS      ?= $(wildcard assets/guns/*.vmg)  # every gun built (GUNS="..." to build in only some)
CXXFLAGS  += -I$(BUILD)
BLENDER   ?= blender
GAMEDATA  ?= /Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData

all: $(DYLIB) $(BAKE)

# The guns built into the mod: every assets/guns/*.vmg (or GUNS). Worked out on every build (embed.py
# rewrites its files only when they change), so building in another set of guns remakes them.
$(BUILD)/gunassets.S $(BUILD)/gunlist.h: tools/guns/embed.py FORCE | $(BUILD)
	@python3 -I tools/guns/embed.py $(BUILD)/gunassets.S $(BUILD)/gunlist.h $(GUNS)

FORCE:

$(DYLIB): $(PLUGIN) $(wildcard src/*.h) $(GUNS) $(BUILD)/gunlist.h $(BUILD)/gunassets.S | $(BUILD)
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

$(BUILD)/test_fpmove: tests/test_fpmove.cpp src/fpmove.cpp src/fpmove.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_fpmove.cpp src/fpmove.cpp

$(BUILD)/test_fplook: tests/test_fplook.cpp src/fplook.cpp src/fplook.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_fplook.cpp src/fplook.cpp

$(BUILD)/test_fpbob: tests/test_fpbob.cpp src/fpbob.cpp src/fpbob.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_fpbob.cpp src/fpbob.cpp

$(BUILD)/test_fpgait: tests/test_fpgait.cpp src/fpgait.cpp src/fpgait.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_fpgait.cpp src/fpgait.cpp

$(BUILD)/test_fontbuf: tests/test_fontbuf.cpp src/fontbuf.cpp src/fontbuf.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_fontbuf.cpp src/fontbuf.cpp

$(BUILD)/test_vmskin: tests/test_vmskin.cpp src/vmskin.cpp src/vmskin.h src/vmmath.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_vmskin.cpp src/vmskin.cpp

$(BUILD)/test_vmarms: tests/test_vmarms.cpp tests/vmtestarm.h src/vmarms.cpp src/vmarms.h src/vmskin.h src/vmmath.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_vmarms.cpp src/vmarms.cpp

$(BUILD)/test_vmgun: tests/test_vmgun.cpp src/vmgun.cpp src/vmgun.h src/vmview.cpp src/vmview.h src/gunassets.cpp $(BUILD)/gunassets.S $(BUILD)/gunlist.h $(GUNS) src/vmmath.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_vmgun.cpp src/vmgun.cpp src/vmview.cpp src/gunassets.cpp $(BUILD)/gunassets.S

$(BUILD)/test_vmpose: tests/test_vmpose.cpp tests/vmtestarm.h src/vmpose.cpp src/vmpose.h src/vmarms.cpp src/vmarms.h src/vmgun.h src/vmskin.h src/vmmath.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_vmpose.cpp src/vmpose.cpp src/vmarms.cpp

$(BUILD)/test_vmview: tests/test_vmview.cpp src/vmview.cpp src/vmview.h src/vmgun.h src/vmmath.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_vmview.cpp src/vmview.cpp

$(BUILD)/test_vmfit: tests/test_vmfit.cpp tests/vmtestarm.h src/vmfit.cpp src/vmfit.h src/vmpose.cpp src/vmpose.h src/vmarms.cpp src/vmskin.cpp src/vmgun.h src/vmmath.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_vmfit.cpp src/vmfit.cpp src/vmpose.cpp src/vmarms.cpp src/vmskin.cpp src/vmgun.cpp

$(BUILD)/test_gunsfx: tests/test_gunsfx.cpp src/gunsfx.cpp src/gunsfx.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_gunsfx.cpp src/gunsfx.cpp

$(BUILD)/test_shotsynth: tests/test_shotsynth.cpp src/shotsynth.cpp src/shotsynth.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_shotsynth.cpp src/shotsynth.cpp

$(BUILD)/test_scope: tests/test_scope.cpp src/scope.cpp src/scope.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_scope.cpp src/scope.cpp

$(BUILD)/test_sfxdsp: tests/test_sfxdsp.cpp src/sfxdsp.cpp src/sfxdsp.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_sfxdsp.cpp src/sfxdsp.cpp

$(BUILD)/test_vmhands: tests/test_vmhands.cpp src/vmhands.cpp src/vmhands.h src/vmpose.cpp src/vmpose.h src/vmarms.cpp src/vmarms.h src/vmgun.cpp src/vmgun.h src/vmanim.cpp src/vmanim.h src/vmskin.h src/vmmath.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_vmhands.cpp src/vmhands.cpp src/vmpose.cpp src/vmarms.cpp src/vmgun.cpp src/vmanim.cpp

$(BUILD)/test_vmanim: tests/test_vmanim.cpp src/vmanim.cpp src/vmanim.h src/vmmath.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_vmanim.cpp src/vmanim.cpp

$(BUILD)/test_fpgun: tests/test_fpgun.cpp src/vmuse.cpp src/vmuse.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_fpgun.cpp src/vmuse.cpp

# The hash of a hook's name as the patcher computes it, so the two are checked against each other.
$(BUILD)/test_hookname: tests/test_hookname.cpp src/hookname.h tools/patch_macvc.py | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -DHOOKNAME_CHUD_DRAW=$$(python3 -I -c 'import sys; sys.path.insert(0, "tools"); import patch_macvc; print(patch_macvc.name_hash("_ZN4CHud4DrawEv"))')u \
		-o $@ tests/test_hookname.cpp

$(BUILD)/test_fpfire: tests/test_fpfire.cpp src/fpfire.cpp src/fpfire.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_fpfire.cpp src/fpfire.cpp

$(BUILD)/test_fpcross: tests/test_fpcross.cpp src/fpcross.cpp src/fpcross.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_fpcross.cpp src/fpcross.cpp

$(BUILD)/test_heat: tests/test_heat.cpp src/heat.cpp src/heat.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_heat.cpp src/heat.cpp

$(BUILD)/test_magnet: tests/test_magnet.cpp src/magnet.cpp src/magnet.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_magnet.cpp src/magnet.cpp

$(BUILD)/test_keys: tests/test_keys.cpp src/keys.cpp src/keys.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_keys.cpp src/keys.cpp

$(BUILD)/test_bindings: tests/test_bindings.cpp src/bindings.cpp src/bindings.h src/layout.h src/keys.cpp src/keys.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_bindings.cpp src/bindings.cpp src/keys.cpp

$(BUILD)/test_hinttext: tests/test_hinttext.cpp src/hinttext.cpp src/hinttext.h src/bindings.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_hinttext.cpp src/hinttext.cpp

$(BUILD)/test_controlsmodel: tests/test_controlsmodel.cpp src/controlsmodel.cpp src/controlsmodel.h src/bindings.cpp src/bindings.h src/layout.h src/keys.cpp src/keys.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_controlsmodel.cpp src/controlsmodel.cpp src/bindings.cpp src/keys.cpp

$(BUILD)/test_inifile: tests/test_inifile.cpp src/inifile.cpp src/inifile.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_inifile.cpp src/inifile.cpp

test: $(BUILD)/test_projection $(BUILD)/test_turns $(BUILD)/test_heat $(BUILD)/test_magnet $(BUILD)/test_keys \
      $(BUILD)/test_bindings $(BUILD)/test_hinttext $(BUILD)/test_controlsmodel \
      $(BUILD)/test_inifile $(BUILD)/test_fpmove $(BUILD)/test_fplook $(BUILD)/test_fpbob $(BUILD)/test_fpgait $(BUILD)/test_fontbuf $(BUILD)/test_vmskin $(BUILD)/test_vmarms $(BUILD)/test_vmgun $(BUILD)/test_vmpose $(BUILD)/test_vmview $(BUILD)/test_vmfit $(BUILD)/test_vmanim $(BUILD)/test_vmhands $(BUILD)/test_sfxdsp $(BUILD)/test_gunsfx $(BUILD)/test_shotsynth $(BUILD)/test_scope $(BUILD)/test_fpgun $(BUILD)/test_hookname $(BUILD)/test_fpfire $(BUILD)/test_fpcross
	$(BUILD)/test_projection
	$(BUILD)/test_turns
	$(BUILD)/test_heat
	$(BUILD)/test_magnet
	$(BUILD)/test_keys
	$(BUILD)/test_bindings
	$(BUILD)/test_hinttext
	$(BUILD)/test_controlsmodel
	$(BUILD)/test_inifile
	$(BUILD)/test_fpmove
	$(BUILD)/test_fplook
	$(BUILD)/test_fpbob
	$(BUILD)/test_fpgait
	$(BUILD)/test_fontbuf
	$(BUILD)/test_vmskin
	$(BUILD)/test_vmarms
	$(BUILD)/test_vmgun
	$(BUILD)/test_vmpose
	$(BUILD)/test_vmview
	$(BUILD)/test_vmfit
	$(BUILD)/test_vmanim
	$(BUILD)/test_vmhands
	$(BUILD)/test_sfxdsp
	$(BUILD)/test_gunsfx
	$(BUILD)/test_shotsynth
	$(BUILD)/test_scope
	$(BUILD)/test_fpgun
	$(BUILD)/test_hookname
	$(BUILD)/test_fpfire
	$(BUILD)/test_fpcross

# The viewmodel's guns, built in Blender from scripts (developers only: the
# built assets are committed). The game's own models are read to measure them.
guns: | $(BUILD)
	mkdir -p $(BUILD)/guns
	for g in $(basename $(notdir $(wildcard assets/guns/*.vmg))); do \
		$(BLENDER) -b -P tools/guns/$$g.py -- "$(GAMEDATA)" assets/guns/$$g.vmg $(BUILD)/guns || exit 1; \
	done

# The viewmodel's offline check: his arms from every outfit, as the mod cuts them.
$(VMTOOL): tools/viewmodel/viewmodel.cpp tools/viewmodel/dff.cpp tools/viewmodel/dff.h $(VMCORE) $(GAMEFILES) $(GUNS) $(BUILD)/gunlist.h $(wildcard src/vm*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -Itools/bake -o $@ tools/viewmodel/viewmodel.cpp tools/viewmodel/dff.cpp $(VMCORE) $(GAMEFILES) -lz

GUN ?= colt45
viewmodel: $(VMTOOL)
	mkdir -p $(BUILD)/vm
	$(VMTOOL) "$(GAMEDATA)" $(BUILD)/vm --gun $(GUN)

# One gun's asset: make gun NAME=python
gun: | $(BUILD)
	mkdir -p $(BUILD)/guns
	$(BLENDER) -b -P tools/guns/$(NAME).py -- "$(GAMEDATA)" assets/guns/$(NAME).vmg $(BUILD)/guns

preview: $(PREVIEW)

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)/*.dylib $(BUILD)/vcmm-* $(BUILD)/test_*

.PHONY: guns gun viewmodel FORCE all test preview clean
