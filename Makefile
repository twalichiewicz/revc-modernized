CXX      := xcrun clang++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -arch arm64 -mmacosx-version-min=15.0
BUILD    := build

DYLIB   := $(BUILD)/libVCModernHUD.dylib
BAKE    := $(BUILD)/vcmm-bake
PREVIEW := $(BUILD)/vcmm-preview
VMTOOL  := $(BUILD)/vcmm-viewmodel
VMCORE  := src/vmanim.cpp src/vmkick.cpp src/vmmelee.cpp src/vmarms.cpp src/vmfit.cpp src/vmgun.cpp src/vmhands.cpp src/vmpose.cpp src/vmskin.cpp src/vmview.cpp src/gunassets.cpp $(BUILD)/gunassets.S

SHARED := src/brickphone.cpp src/canvas.cpp src/mapdata.cpp src/projection.cpp src/scene.cpp
PLUGIN := $(SHARED) src/actions.cpp src/aimcam.cpp src/aimslow.cpp src/alert.cpp src/anims.cpp src/bindings.cpp src/body.cpp src/camera.cpp src/carlights.cpp src/carradio.cpp src/climb.cpp src/clouds.cpp src/combat.cpp src/config.cpp src/consolespot.cpp src/controls.cpp src/controlsmodel.cpp src/controlspage.cpp src/credit.cpp src/crouch.cpp src/doorgrab.cpp src/falldamage.cpp src/falls.cpp src/shapes.cpp src/shootdodge.cpp src/sight.cpp src/spotters.cpp src/draw.cpp src/driveby.cpp src/game.cpp src/grade.cpp src/fontbuf.cpp src/fpbob.cpp src/flightpath.cpp src/fpcross.cpp src/fpfire.cpp src/fpgait.cpp src/fplook.cpp src/fpshoot.cpp src/fpmove.cpp src/fpwalk.cpp src/gps.cpp src/gridclip.cpp src/gridfix.cpp src/grip.cpp src/gunbank.cpp src/gunsfx.cpp src/heat.cpp src/heli.cpp src/helizones.cpp src/hints.cpp src/hinttext.cpp src/hooks.cpp src/hunter.cpp src/hud.cpp src/inifile.cpp src/magnet.cpp src/interact.cpp src/keys.cpp src/log.cpp src/menu.cpp src/minimap.cpp src/phone.cpp src/places.cpp src/planes.cpp src/player.cpp src/poi.cpp src/police.cpp src/prompt.cpp src/radiohud.cpp src/radiologos.cpp src/rtshadows.cpp src/rtgeometry.cpp src/scanner.cpp src/streethud.cpp src/rainmodel.cpp src/weather.cpp src/wetmodel.cpp src/wetroads.cpp src/worlddraw.cpp src/rainstreaks.cpp src/raindrops.cpp src/dropmodel.cpp src/gamedrops.cpp src/vehiclerain.cpp src/rainvehicles.cpp src/streetmodel.cpp src/rumble.mm src/scope.cpp src/seateye.cpp src/seatgun.cpp src/seatview.cpp src/sfxdsp.cpp src/shaders.cpp src/shotsound.cpp src/shotsynth.cpp src/sniper.cpp src/sky.cpp src/steering.cpp src/story.cpp src/achievements.cpp src/achievementsmodel.cpp src/statsmodel.cpp src/statspage.cpp src/swim.cpp src/text.cpp src/traffic.cpp src/turns.cpp src/viewmodel.cpp src/vmanim.cpp src/vmkick.cpp src/vmmelee.cpp src/vmarms.cpp src/vmfit.cpp src/vmgun.cpp src/vmhands.cpp src/vmpose.cpp src/vmskin.cpp src/vmuse.cpp src/vmview.cpp src/fpgun.cpp src/gunassets.cpp $(BUILD)/gunassets.S src/wanted.cpp src/wantedhud.cpp src/watch.cpp src/witness.cpp src/wheel.cpp
GAMEFILES := tools/bake/gamefiles.cpp tools/bake/visualmodel.cpp tools/bake/streets.cpp tools/bake/wetground.cpp
PLUGIN += src/aircraft.cpp src/checkpoint.cpp src/checkpointmodel.cpp src/drivers.cpp src/fpfeedback.cpp \
          src/greeting.cpp src/intimidation.cpp src/population.cpp src/populationmodel.cpp src/reactions.cpp src/reactphysics.cpp src/reactrig.cpp src/reactcontact.cpp \
          src/reticle.cpp src/reticlemodel.cpp src/swimmotion.cpp src/voiceclip.cpp src/wheelgrip.cpp \
          src/gameplaypage.cpp src/gameplaymodel.cpp src/reactself.cpp
PLUGIN += src/reactcrowd.cpp src/reacthit.cpp
PLUGIN += src/shotimpact.cpp src/reactimpact.cpp src/playerreact.cpp src/playerreactmodel.cpp
PLUGIN += src/windowmode.mm
PLUGIN += src/estatelighting.cpp
PLUGIN += src/ballistics.cpp
PLUGIN += src/shotcollision.cpp
PLUGIN += src/planeflight.cpp src/flightmodel.cpp
PLUGIN += src/airportjets.cpp src/aircraftdamage.cpp
PLUGIN += src/airportjetmodel.cpp src/jetwheels.cpp src/jetaudio.cpp src/jetlights.cpp
PLUGIN += src/airworld.cpp
PLUGIN += src/jetwash.cpp
PLUGIN += src/trafficweave.cpp
PLUGIN += src/towercollision.cpp
PLUGIN += src/jetshadow.cpp
PLUGIN += src/jetboardingpose.cpp
PLUGIN += src/handlingmodel.cpp src/handling.cpp src/drivelog.cpp src/headbalance.cpp
PLUGIN += src/glassmodel.cpp src/carglass.cpp
PLUGIN += src/partsmodel.cpp src/carparts.cpp
PLUGIN += src/dentmodel.cpp src/carimpact.cpp
PLUGIN += src/mechanicalmodel.cpp src/carmechanics.cpp
GUNS      ?= $(wildcard assets/guns/*.vmg)  # every gun built (GUNS="..." to build in only some)
CXXFLAGS  += -I$(BUILD)
BLENDER   ?= blender
GAMEDATA  ?= /Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData
MACVC_NATIVE ?= $(HOME)/Library/Application Support/macVC/vcmodernhud/macVC.vanilla

all: $(DYLIB) $(BAKE)

# The guns built into the mod: every assets/guns/*.vmg (or GUNS). Worked out on every build (embed.py
# rewrites its files only when they change), so building in another set of guns remakes them.
$(BUILD)/gunassets.S $(BUILD)/gunlist.h: tools/guns/embed.py FORCE | $(BUILD)
	@python3 -I tools/guns/embed.py $(BUILD)/gunassets.S $(BUILD)/gunlist.h $(GUNS)

FORCE:

$(DYLIB): $(PLUGIN) $(wildcard src/*.h) $(GUNS) $(BUILD)/gunlist.h $(BUILD)/gunassets.S | $(BUILD)
	$(CXX) $(CXXFLAGS) -fvisibility=hidden -fobjc-arc -dynamiclib \
		-install_name @executable_path/../Frameworks/libVCModernHUD.dylib -o $@ $(PLUGIN) \
		-framework Foundation -framework AppKit -framework GameController -framework CoreHaptics -framework AudioToolbox

$(BUILD)/test_aircraft: tests/test_aircraft.cpp src/aircraft.cpp src/aircraft.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_aircraft.cpp src/aircraft.cpp

$(BUILD)/test_apb: tests/test_apb.cpp src/heat.cpp src/heat.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_apb.cpp src/heat.cpp

$(BUILD)/test_helilight: tests/test_helilight.cpp src/helizones.cpp src/helizones.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_helilight.cpp src/helizones.cpp

$(BUILD)/test_fpfeedback: tests/test_fpfeedback.cpp src/fpfeedback.cpp src/fpfeedback.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_fpfeedback.cpp src/fpfeedback.cpp

$(BUILD)/test_swimmotion: tests/test_swimmotion.cpp src/swimmotion.cpp src/swimmotion.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_swimmotion.cpp src/swimmotion.cpp

$(BUILD)/test_wheelgrip: tests/test_wheelgrip.cpp src/wheelgrip.cpp src/wheelgrip.h src/vmpose.cpp src/vmarms.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_wheelgrip.cpp src/wheelgrip.cpp src/vmpose.cpp src/vmarms.cpp

$(BUILD)/test_reticlemodel: tests/test_reticlemodel.cpp src/reticlemodel.cpp src/fpcross.cpp src/inifile.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_reticlemodel.cpp src/reticlemodel.cpp src/fpcross.cpp src/inifile.cpp

$(BUILD)/test_gameplaymodel: tests/test_gameplaymodel.cpp src/gameplaymodel.cpp src/gameplaymodel.h src/reticlemodel.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_gameplaymodel.cpp src/gameplaymodel.cpp

$(BUILD)/test_menulabels: tests/test_menulabels.cpp src/menulabels.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_menulabels.cpp

$(BUILD)/test_estatelighting: tests/test_estatelighting.cpp src/grade.cpp src/grade.h src/shaders.h src/rainmodel.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_estatelighting.cpp src/rainmodel.cpp

$(BUILD)/test_carlightrules: tests/test_carlightrules.cpp src/carlightrules.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_carlightrules.cpp

$(BUILD)/test_roofgeometry: tests/test_roofgeometry.cpp tools/bake/roofgeometry.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_roofgeometry.cpp

$(BUILD)/test_streetsnap: tests/test_streetsnap.cpp tools/bake/streets.cpp tools/bake/streets.h src/mapdata.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_streetsnap.cpp tools/bake/streets.cpp

$(BUILD)/test_streethud: tests/test_streethud.cpp src/streethud.cpp src/streethud.h src/streetmodel.cpp src/streetmodel.h src/mapdata.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_streethud.cpp

$(BUILD)/test_wetground: tests/test_wetground.cpp tools/bake/wetground.cpp tools/bake/wetground.h src/mapdata.cpp src/mapdata.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -Itools/bake -o $@ tests/test_wetground.cpp tools/bake/wetground.cpp src/mapdata.cpp

$(BUILD)/test_wetmodel: tests/test_wetmodel.cpp src/wetmodel.cpp src/wetmodel.h src/mapdata.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_wetmodel.cpp src/wetmodel.cpp

$(BUILD)/test_dropmodel: tests/test_dropmodel.cpp src/dropmodel.cpp src/dropmodel.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_dropmodel.cpp src/dropmodel.cpp

$(BUILD)/test_worlddraw: tests/test_worlddraw.cpp src/worlddraw.cpp src/worlddraw.h src/game.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_worlddraw.cpp

$(BUILD)/test_gamedrops: tests/test_gamedrops.cpp src/gamedrops.cpp src/gamedrops.h src/config.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_gamedrops.cpp

$(BUILD)/test_haze: tests/test_haze.cpp src/weather.cpp src/weather.h src/rainmodel.cpp src/rainmodel.h src/config.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_haze.cpp src/rainmodel.cpp

$(BUILD)/test_vehiclerain: tests/test_vehiclerain.cpp src/vehiclerain.cpp src/vehiclerain.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_vehiclerain.cpp src/vehiclerain.cpp

$(BUILD)/test_rainvehicles: tests/test_rainvehicles.cpp src/rainvehicles.cpp src/rainvehicles.h src/vehiclerain.cpp src/rainmodel.cpp src/mapdata.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_rainvehicles.cpp src/vehiclerain.cpp src/rainmodel.cpp src/mapdata.cpp

$(BUILD)/test_handlingmodel: tests/test_handlingmodel.cpp src/handlingmodel.cpp src/handlingmodel.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_handlingmodel.cpp src/handlingmodel.cpp

$(BUILD)/test_glassmodel: tests/test_glassmodel.cpp src/glassmodel.cpp src/glassmodel.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_glassmodel.cpp src/glassmodel.cpp

$(BUILD)/test_carglass: tests/test_carglass.cpp src/carglass.cpp src/carglass.h src/glassmodel.cpp src/dentmodel.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_carglass.cpp src/glassmodel.cpp src/dentmodel.cpp

$(BUILD)/test_partsmodel: tests/test_partsmodel.cpp src/partsmodel.cpp src/partsmodel.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_partsmodel.cpp src/partsmodel.cpp

$(BUILD)/test_carparts: tests/test_carparts.cpp tests/nativepartdamagearm.h tests/nativeweavearm.h src/carparts.cpp src/partsmodel.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_carparts.cpp src/partsmodel.cpp

test-game-assets: $(BUILD)/test_carparts $(BUILD)/test_cardeform $(BUILD)/test_mechanical_assets $(BUILD)/test_mechanical_native

# Optional real mesh-builder regression, using an explicitly supplied NULL librw build.
.PHONY: test-carglass-rw
test-carglass-rw: | $(BUILD)
	@test -n "$(LIBRW_ROOT)" -a -n "$(LIBRW_STATIC)" || { echo 'Set LIBRW_ROOT and LIBRW_STATIC to a NULL-backend librw source/build'; exit 2; }
	$(CXX) $(CXXFLAGS) -DRW_NULL -isystem "$(LIBRW_ROOT)" -Isrc -o $(BUILD)/test_carglass_rw tests/test_carglass_rw.cpp src/glassmodel.cpp src/dentmodel.cpp "$(LIBRW_STATIC)"
	$(BUILD)/test_carglass_rw

$(BUILD)/test_glass_assets: tests/test_glass_assets.cpp src/glassmodel.cpp tools/bake/visualmodel.cpp tools/bake/gamefiles.cpp src/mapdata.cpp $(wildcard src/*.h tools/bake/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_glass_assets.cpp src/glassmodel.cpp tools/bake/visualmodel.cpp tools/bake/gamefiles.cpp src/mapdata.cpp -lz

$(BUILD)/test_handling_assets: tests/test_handling_assets.cpp src/handlingmodel.cpp src/handlingmodel.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_handling_assets.cpp src/handlingmodel.cpp

$(BUILD)/test_handlinghook: tests/test_handlinghook.cpp src/handling.cpp src/handling.h src/handlingmodel.cpp src/handlingmodel.h src/drivelog.cpp src/drivelog.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_handlinghook.cpp src/handlingmodel.cpp src/drivelog.cpp

$(BUILD)/test_drivelog: tests/test_drivelog.cpp src/drivelog.cpp src/drivelog.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_drivelog.cpp src/drivelog.cpp

$(BUILD)/test_headbalance: tests/test_headbalance.cpp src/headbalance.cpp src/headbalance.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_headbalance.cpp src/headbalance.cpp

$(BUILD)/test_rainmodel: tests/test_rainmodel.cpp src/rainmodel.cpp src/rainmodel.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_rainmodel.cpp src/rainmodel.cpp

$(BUILD)/test_streetmodel: tests/test_streetmodel.cpp src/streetmodel.cpp src/streetmodel.h src/mapdata.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_streetmodel.cpp src/streetmodel.cpp

$(BUILD)/test_mapstreets: tests/test_mapstreets.cpp src/mapdata.cpp src/mapdata.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_mapstreets.cpp src/mapdata.cpp

$(BUILD)/test_mapheights: tests/test_mapheights.cpp src/mapdata.cpp src/mapdata.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_mapheights.cpp src/mapdata.cpp

$(BUILD)/test_checkpointmodel: tests/test_checkpointmodel.cpp src/checkpointmodel.cpp src/checkpointmodel.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_checkpointmodel.cpp src/checkpointmodel.cpp

$(BUILD)/test_voiceclip: tests/test_voiceclip.cpp src/voiceclip.cpp src/voiceclip.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_voiceclip.cpp src/voiceclip.cpp

$(BUILD)/test_intimidation: tests/test_intimidation.cpp src/intimidation.cpp src/intimidation.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_intimidation.cpp src/intimidation.cpp

$(BUILD)/test_populationmodel: tests/test_populationmodel.cpp src/populationmodel.cpp src/populationmodel.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_populationmodel.cpp src/populationmodel.cpp

$(BUILD)/test_reactphysics: tests/test_reactphysics.cpp src/reactphysics.cpp src/reactphysics.h src/reactself.cpp src/reactself.h src/reactshape.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_reactphysics.cpp src/reactphysics.cpp src/reactself.cpp

$(BUILD)/test_reactsolver: tests/test_reactsolver.cpp tests/reactworld.h src/reactphysics.cpp src/reactphysics.h src/reactself.cpp src/reactself.h src/reactcontact.cpp src/reactcontact.h src/reactrig.cpp src/reactshape.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_reactsolver.cpp src/reactphysics.cpp src/reactself.cpp src/reactcontact.cpp src/reactrig.cpp

$(BUILD)/test_reactcontact_native: tests/test_reactcontact_native.cpp src/reactcontact.cpp src/reactcontact.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_reactcontact_native.cpp

$(BUILD)/test_reactcrowd: tests/test_reactcrowd.cpp tests/reactworld.h src/reactcrowd.cpp $(wildcard src/react*.h) src/reactphysics.cpp src/reactself.cpp src/reactcontact.cpp src/reactrig.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_reactcrowd.cpp src/reactcrowd.cpp src/reactphysics.cpp src/reactself.cpp src/reactcontact.cpp src/reactrig.cpp

$(BUILD)/test_reactworldhook: tests/test_reactworldhook.cpp tests/test_reactionhook.cpp src/reactions.cpp src/reactcrowd.cpp $(wildcard src/react*.h) src/reactphysics.cpp src/reactself.cpp src/reactcontact.cpp src/reactrig.cpp src/reactimpact.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_reactworldhook.cpp src/reactcrowd.cpp src/reactphysics.cpp src/reactself.cpp src/reactcontact.cpp src/reactrig.cpp src/reactimpact.cpp

$(BUILD)/test_reactnavigation: tests/test_reactnavigation.cpp tests/test_reactionhook.cpp src/reactions.cpp $(wildcard src/react*.h) src/reactcrowd.cpp src/reactphysics.cpp src/reactself.cpp src/reactcontact.cpp src/reactrig.cpp src/reactimpact.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_reactnavigation.cpp src/reactcrowd.cpp src/reactphysics.cpp src/reactself.cpp src/reactcontact.cpp src/reactrig.cpp src/reactimpact.cpp

$(BUILD)/test_playerragdoll: tests/test_playerragdoll.cpp tests/test_reactionhook.cpp src/reactions.cpp $(wildcard src/react*.h) src/reactcrowd.cpp src/reactphysics.cpp src/reactself.cpp src/reactcontact.cpp src/reactrig.cpp src/reactimpact.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_playerragdoll.cpp src/reactcrowd.cpp src/reactphysics.cpp src/reactself.cpp src/reactcontact.cpp src/reactrig.cpp src/reactimpact.cpp

$(BUILD)/test_reactcorpse: tests/test_reactcorpse.cpp tests/test_reactionhook.cpp src/reactions.cpp $(wildcard src/react*.h) src/reactcrowd.cpp src/reactphysics.cpp src/reactself.cpp src/reactcontact.cpp src/reactrig.cpp src/reactimpact.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_reactcorpse.cpp src/reactcrowd.cpp src/reactphysics.cpp src/reactself.cpp src/reactcontact.cpp src/reactrig.cpp src/reactimpact.cpp

$(BUILD)/test_reactrecovery: tests/test_reactrecovery.cpp tests/reactworld.h src/reactphysics.cpp src/reactself.cpp src/reactcontact.cpp src/reactrig.cpp $(wildcard src/react*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_reactrecovery.cpp src/reactphysics.cpp src/reactself.cpp src/reactcontact.cpp src/reactrig.cpp

$(BUILD)/test_recoveryhook: tests/test_recoveryhook.cpp tests/test_reactionhook.cpp src/reactions.cpp $(wildcard src/react*.h) src/reactcrowd.cpp src/reactphysics.cpp src/reactself.cpp src/reactcontact.cpp src/reactrig.cpp src/reactimpact.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_recoveryhook.cpp src/reactcrowd.cpp src/reactphysics.cpp src/reactself.cpp src/reactcontact.cpp src/reactrig.cpp src/reactimpact.cpp

$(BUILD)/test_reacthit_native: tests/test_reacthit_native.cpp tests/test_reactionhook.cpp tests/nativejetshadowarm.h tests/nativeweavearm.h src/reacthit.cpp src/reactions.cpp $(wildcard src/react*.h) src/reactcrowd.cpp src/reactphysics.cpp src/reactself.cpp src/reactcontact.cpp src/reactrig.cpp src/reactimpact.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_reacthit_native.cpp src/reactcrowd.cpp src/reactphysics.cpp src/reactself.cpp src/reactcontact.cpp src/reactrig.cpp src/reactimpact.cpp

$(BUILD)/test_reacthit: tests/test_reacthit.cpp tests/nativejetshadowarm.h tests/nativeweavearm.h src/reacthit.cpp $(wildcard src/react*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_reacthit.cpp

$(BUILD)/test_reactself: tests/test_reactself.cpp src/reactself.cpp src/reactself.h src/reactphysics.cpp src/reactphysics.h src/reactshape.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_reactself.cpp src/reactself.cpp src/reactphysics.cpp

$(BUILD)/test_reactrig: tests/test_reactrig.cpp src/reactrig.cpp src/reactrig.h src/vmmath.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_reactrig.cpp src/reactrig.cpp

$(BUILD)/test_carmaterials: tests/test_carmaterials.cpp src/carmaterials.h src/carlightrules.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_carmaterials.cpp

$(BUILD)/test_populationstream: tests/test_populationstream.cpp src/populationrules.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_populationstream.cpp

$(BUILD)/test_reactcontact: tests/test_reactcontact.cpp tests/reactworld.h src/reactcontact.cpp src/reactcontact.h src/reactphysics.cpp src/reactphysics.h src/reactself.cpp src/reactself.h src/reactrig.cpp src/reactrig.h src/reactshape.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_reactcontact.cpp src/reactcontact.cpp src/reactphysics.cpp src/reactself.cpp src/reactrig.cpp

$(BUILD)/test_reactionhook: tests/test_reactionhook.cpp src/reactions.cpp src/reactphysics.cpp src/reactrig.cpp src/reactcontact.cpp src/reactself.cpp src/reactself.h src/reactimpact.cpp src/reactimpact.h src/shotimpact.h src/playerreact.h src/config.h src/reactcrowd.cpp src/reactcrowd.h src/reactshape.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_reactionhook.cpp src/reactphysics.cpp src/reactrig.cpp src/reactcontact.cpp src/reactself.cpp src/reactimpact.cpp src/reactcrowd.cpp

$(BUILD)/test_shotimpact: tests/test_shotimpact.cpp src/shotimpact.cpp src/shotimpact.h src/hooks.h src/game.h src/config.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_shotimpact.cpp

$(BUILD)/test_hookchain: tests/test_hookchain.cpp src/hooks.cpp src/hooks.h src/hookname.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_hookchain.cpp

$(BUILD)/test_reactimpact: tests/test_reactimpact.cpp src/reactimpact.cpp src/reactimpact.h src/reactphysics.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_reactimpact.cpp src/reactimpact.cpp

$(BUILD)/test_playerreact: tests/test_playerreact.cpp tests/playerreactrig.h src/playerreactmodel.cpp src/playerreact.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_playerreact.cpp src/playerreactmodel.cpp

$(BUILD)/test_playerreacthook: tests/test_playerreacthook.cpp src/player.cpp src/playerreact.cpp src/playerreactmodel.cpp src/playerreact.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_playerreacthook.cpp src/playerreact.cpp src/playerreactmodel.cpp

$(BUILD)/test_playerreactview: tests/test_playerreactview.cpp tests/vmtestarm.h $(wildcard assets/guns/*.vmg) src/fpgun.cpp src/playerreactmodel.cpp src/playerreact.h $(wildcard src/vm*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -Wl,-dead_strip -o $@ tests/test_playerreactview.cpp src/playerreactmodel.cpp src/vmarms.cpp src/vmpose.cpp src/vmhands.cpp src/vmfit.cpp src/vmgun.cpp src/vmanim.cpp src/vmkick.cpp src/vmmelee.cpp src/vmskin.cpp src/vmuse.cpp src/vmview.cpp

$(BUILD)/test_windowmode: tests/test_windowmode.mm src/windowmode.mm src/windowmode.h src/inifile.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -fobjc-arc -o $@ tests/test_windowmode.mm src/inifile.cpp -framework AppKit

$(BUILD)/test_aimfire: tests/test_aimfire.cpp src/aimcam.cpp src/body.cpp src/fpfeedback.cpp src/fpfire.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -Wl,-dead_strip -o $@ tests/test_aimfire.cpp src/fpfeedback.cpp src/fpfire.cpp

$(BUILD)/test_aimposehook: tests/test_aimposehook.cpp src/aimcam.cpp src/body.cpp src/fpfeedback.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -Wl,-dead_strip -o $@ tests/test_aimposehook.cpp src/fpfeedback.cpp

$(BUILD)/test_aimspreadhook: tests/test_aimspreadhook.cpp tests/test_damageoverlayhook.cpp src/player.cpp src/camera.cpp src/headbalance.cpp src/headbalance.h src/fpfeedback.cpp src/draw.cpp src/canvas.cpp src/fpbob.cpp src/scope.cpp src/game.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -Wl,-dead_strip -o $@ tests/test_aimspreadhook.cpp src/fpfeedback.cpp src/draw.cpp src/canvas.cpp src/headbalance.cpp src/fpbob.cpp src/scope.cpp src/game.cpp

$(BUILD)/test_damageoverlayhook: tests/test_damageoverlayhook.cpp src/player.cpp src/camera.cpp src/headbalance.cpp src/headbalance.h src/fpfeedback.cpp src/draw.cpp src/canvas.cpp src/fpbob.cpp src/scope.cpp src/game.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -Wl,-dead_strip -o $@ tests/test_damageoverlayhook.cpp src/fpfeedback.cpp src/draw.cpp src/canvas.cpp src/headbalance.cpp src/fpbob.cpp src/scope.cpp src/game.cpp

$(BUILD)/test_aircraftcamerahook: tests/test_aircraftcamerahook.cpp tests/test_damageoverlayhook.cpp src/player.cpp src/camera.cpp src/headbalance.cpp src/headbalance.h src/planeflight.h src/flightinput.h src/fpfeedback.cpp src/draw.cpp src/canvas.cpp src/fpbob.cpp src/scope.cpp src/game.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -Wl,-dead_strip -o $@ tests/test_aircraftcamerahook.cpp src/fpfeedback.cpp src/draw.cpp src/canvas.cpp src/headbalance.cpp src/fpbob.cpp src/scope.cpp src/game.cpp

$(BUILD)/test_helihook: tests/test_helihook.cpp src/heli.cpp src/helizones.cpp src/aircraft.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_helihook.cpp src/helizones.cpp src/aircraft.cpp

$(BUILD)/test_npcthreat: tests/test_npcthreat.cpp src/drivers.cpp src/intimidation.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_npcthreat.cpp src/intimidation.cpp

$(BUILD)/test_policedrive: tests/test_policedrive.cpp src/policedrive.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_policedrive.cpp

$(BUILD)/test_policearrive: tests/test_policearrive.cpp src/police.cpp src/heat.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -Wl,-dead_strip -o $@ tests/test_policearrive.cpp src/heat.cpp

$(BUILD)/test_policepullup: tests/test_policepullup.cpp src/police.cpp src/heat.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -Wl,-dead_strip -o $@ tests/test_policepullup.cpp src/heat.cpp

$(BUILD)/test_policefootsearch: tests/test_policefootsearch.cpp src/police.cpp src/heat.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -Wl,-dead_strip -o $@ tests/test_policefootsearch.cpp src/heat.cpp

$(BUILD)/test_achievements: tests/test_achievements.cpp src/achievements.cpp src/achievementsmodel.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_achievements.cpp

$(BUILD)/test_statsmodel: tests/test_statsmodel.cpp src/statsmodel.cpp src/statsmodel.h src/achievementsmodel.cpp src/achievementsmodel.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_statsmodel.cpp src/statsmodel.cpp src/achievementsmodel.cpp

$(BUILD)/test_rtgeometry: tests/test_rtgeometry.cpp src/rtgeometry.cpp src/rtgeometry.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_rtgeometry.cpp src/rtgeometry.cpp

$(BUILD)/test_rtshadowshook: tests/test_rtshadowshook.cpp src/rtshadows.cpp src/rtgeometry.cpp src/rtgeometry.h src/rtshadows.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_rtshadowshook.cpp src/rtgeometry.cpp

$(BUILD)/test_storycredit: tests/test_storycredit.cpp src/story.cpp src/credit.cpp src/story.h src/credit.h src/game.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_storycredit.cpp

$(BUILD)/test_scanner: tests/test_scanner.cpp src/scanner.cpp src/scanner.h src/game.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_scanner.cpp

$(BUILD)/test_achievementsmodel: tests/test_achievementsmodel.cpp src/achievementsmodel.cpp src/achievementsmodel.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_achievementsmodel.cpp src/achievementsmodel.cpp

$(BUILD)/test_policemove: tests/test_policemove.cpp src/policemove.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_policemove.cpp

$(BUILD)/test_policeseek: tests/test_policeseek.cpp src/police.cpp src/heat.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -Wl,-dead_strip -o $@ tests/test_policeseek.cpp src/heat.cpp

$(BUILD)/test_populationroute: tests/test_populationroute.cpp src/population.cpp src/populationmodel.cpp src/police.cpp src/heat.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_populationroute.cpp src/populationmodel.cpp src/heat.cpp

$(BUILD)/test_visualmodel: tests/test_visualmodel.cpp tools/bake/visualmodel.cpp tools/bake/visualmodel.h tools/bake/gamefiles.cpp tools/bake/gamefiles.h src/mapdata.cpp src/mapdata.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_visualmodel.cpp tools/bake/visualmodel.cpp tools/bake/gamefiles.cpp src/mapdata.cpp -lz

$(BUILD)/test_mapvisual: tests/test_mapvisual.cpp src/scene.cpp src/projection.cpp src/canvas.cpp src/mapdata.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_mapvisual.cpp src/projection.cpp src/canvas.cpp src/mapdata.cpp

$(BUILD)/test_estatehook: tests/test_estatehook.cpp src/estatelighting.cpp src/estatelighting.h src/hooks.h src/types.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_estatehook.cpp

$(BUILD)/test_estateassets: tests/test_estateassets.cpp src/estatelighting.cpp src/estatelighting.h tools/bake/gamefiles.cpp tools/bake/gamefiles.h src/types.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_estateassets.cpp tools/bake/gamefiles.cpp -lz

$(BUILD)/test_mapassets: tests/test_mapassets.cpp tools/bake/bake.cpp $(GAMEFILES) $(wildcard tools/bake/*.h) src/mapdata.cpp src/mapdata.h src/radiologos.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_mapassets.cpp $(GAMEFILES) src/mapdata.cpp src/radiologos.cpp -lz

test-game-assets: $(BUILD)/test_glass_assets $(BUILD)/test_estateassets $(BUILD)/test_handling_assets $(BUILD)/test_mapassets $(BUILD)/test_shotcollision $(BUILD)/test_rotorassets $(BUILD)/test_planeflight_assets $(BUILD)/test_flightpath_assets $(BUILD)/test_planeflight_nativecaller $(BUILD)/test_airportjets_assets $(BUILD)/test_planestream $(BUILD)/test_airportjetmodel $(BUILD)/test_airworld_native $(BUILD)/test_learjetseat_assets $(BUILD)/test_jetwash_assets $(BUILD)/test_trafficweave $(BUILD)/test_towercollision_assets $(BUILD)/test_towercollision_native $(BUILD)/test_jetboarding_assets $(BUILD)/test_jetboarding_native $(BUILD)/test_jetshadow_assets $(BUILD)/test_jetshadow_native $(BUILD)/test_jetboardingpose $(BUILD)/test_airportjetspawners_native $(BUILD)/test_jetcrouchgait
	GAMEDATA="$(GAMEDATA)" $(BUILD)/test_glass_assets
	MACVC_NATIVE="$(MACVC_NATIVE)" $(BUILD)/test_carparts --native
	MACVC_NATIVE="$(MACVC_NATIVE)" $(BUILD)/test_cardeform --native
	GAMEDATA="$(GAMEDATA)" $(BUILD)/test_mechanical_assets
	MACVC_NATIVE="$(MACVC_NATIVE)" $(BUILD)/test_mechanical_native
	GAMEDATA="$(GAMEDATA)" $(BUILD)/test_estateassets
	GAMEDATA="$(GAMEDATA)" $(BUILD)/test_handling_assets
	GAMEDATA="$(GAMEDATA)" $(BUILD)/test_mapassets
	GAMEDATA="$(GAMEDATA)" $(BUILD)/test_shotcollision --assets
	GAMEDATA="$(GAMEDATA)" $(BUILD)/test_rotorassets
	GAMEDATA="$(GAMEDATA)" $(BUILD)/test_planeflight_assets
	GAMEDATA="$(GAMEDATA)" $(BUILD)/test_flightpath_assets
	GAMEDATA="$(GAMEDATA)" $(BUILD)/test_planeflight_nativecaller
	GAMEDATA="$(GAMEDATA)" $(BUILD)/test_airportjets_assets
	GAMEDATA="$(GAMEDATA)" $(BUILD)/test_planestream
	GAMEDATA="$(GAMEDATA)" $(BUILD)/test_airportjetmodel
	GAMEDATA="$(GAMEDATA)" $(BUILD)/test_airworld_native
	GAMEDATA="$(GAMEDATA)" $(BUILD)/test_learjetseat_assets
	GAMEDATA="$(GAMEDATA)" $(BUILD)/test_jetwash_assets
	GAMEDATA="$(GAMEDATA)" MACVC_NATIVE="$(MACVC_NATIVE)" $(BUILD)/test_trafficweave --native
	GAMEDATA="$(GAMEDATA)" $(BUILD)/test_towercollision_assets
	GAMEDATA="$(GAMEDATA)" MACVC_NATIVE="$(MACVC_NATIVE)" $(BUILD)/test_towercollision_native
	GAMEDATA="$(GAMEDATA)" $(BUILD)/test_jetboarding_assets
	GAMEDATA="$(GAMEDATA)" MACVC_NATIVE="$(MACVC_NATIVE)" $(BUILD)/test_jetboarding_native
	GAMEDATA="$(GAMEDATA)" MACVC_NATIVE="$(MACVC_NATIVE)" $(BUILD)/test_jetshadow_assets
	GAMEDATA="$(GAMEDATA)" MACVC_NATIVE="$(MACVC_NATIVE)" $(BUILD)/test_jetshadow_native
	GAMEDATA="$(GAMEDATA)" $(BUILD)/test_jetboardingpose
	GAMEDATA="$(GAMEDATA)" $(BUILD)/test_jetcrouchgait
	GAMEDATA="$(GAMEDATA)" MACVC_NATIVE="$(MACVC_NATIVE)" $(BUILD)/test_airportjetspawners_native
	python3 -I tests/test_patchcapacity.py

$(BUILD)/test_airportjets_assets: tests/test_airportjets_assets.cpp $(GAMEFILES) $(wildcard tools/bake/*.h) src/mapdata.cpp src/mapdata.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_airportjets_assets.cpp $(GAMEFILES) src/mapdata.cpp -lz

$(BUILD)/test_planestream: tests/test_planestream.cpp tests/test_planeflight.cpp src/planeflight.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_planestream.cpp

$(BUILD)/test_airportjetmodel: tests/test_airportjetmodel.cpp src/airportjetmodel.cpp tools/bake/gamefiles.cpp $(wildcard src/*.h tools/bake/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_airportjetmodel.cpp tools/bake/gamefiles.cpp -lz

$(BUILD)/test_drivershook: tests/test_drivershook.cpp src/drivers.cpp src/intimidation.cpp src/config.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_drivershook.cpp src/intimidation.cpp

$(BUILD)/test_trafficinstall: tests/test_trafficinstall.cpp src/traffic.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_trafficinstall.cpp

FEEDBACK_TESTS := test_aircraft test_apb test_helilight test_fpfeedback test_swimmotion test_wheelgrip \
                  test_reticlemodel test_carlightrules test_roofgeometry test_mapheights test_checkpointmodel \
                  test_voiceclip test_intimidation test_populationmodel test_reactphysics test_reactsolver test_reactcontact_native test_reactcrowd test_reactworldhook test_reactself test_reactrig test_carmaterials test_populationstream \
                  test_reactnavigation test_reactcorpse test_playerragdoll test_reactrecovery test_recoveryhook test_reacthit test_reacthit_native \
                  test_reactcontact test_reactionhook test_drivershook test_trafficinstall \
                  test_shotimpact test_hookchain test_reactimpact test_playerreact test_playerreacthook test_playerreactview test_windowmode \
                  test_aimposehook test_aimfire test_aimspreadhook test_damageoverlayhook test_helihook test_npcthreat test_populationroute test_achievementsmodel test_achievements test_statsmodel test_rtgeometry test_rtshadowshook test_storycredit test_scanner test_mapstreets test_streetsnap test_streetmodel test_streethud test_rainmodel test_wetground test_wetmodel test_dropmodel test_worlddraw test_gamedrops test_haze test_vehiclerain test_rainvehicles test_policemove test_policeseek test_policedrive test_policearrive test_policepullup test_policefootsearch test_visualmodel test_mapvisual test_estatehook test_ballistics test_pedcontacthook test_shotcollision test_hunterhook \
                  test_flightmodelhook test_flightmodel_hunter test_planeflight test_planeflight_roster test_fpwalkcontacthook test_flightownership_native test_aircraftcamerahook test_airportjets test_aircraftdamage test_jetlights test_jetwheel_flight test_jetwheels test_jetaudio test_gridfixhook test_planestability test_planeengine test_planeglide test_jetground test_airworld test_jetwash test_trafficweave test_towercollision test_jetboarding test_airportjetspawners

$(BUILD)/test_jetboarding: tests/test_jetboarding.cpp tests/test_airportjets.cpp tests/test_planeflight.cpp src/airportjets.cpp src/jetboarding.h $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_jetboarding.cpp

$(BUILD)/test_airportjetspawners: tests/test_airportjetspawners.cpp tests/test_airportjets.cpp tests/test_planeflight.cpp src/airportjets.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_airportjetspawners.cpp

$(BUILD)/test_jetboarding_assets: tests/test_jetboarding_assets.cpp tests/test_airportjets.cpp tests/test_planeflight.cpp src/airportjets.cpp src/jetboarding.h tools/bake/gamefiles.cpp $(wildcard src/*.h tools/bake/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_jetboarding_assets.cpp tools/bake/gamefiles.cpp -lz

$(BUILD)/test_jetboarding_native: tests/test_jetboarding_native.cpp tests/test_towercollision_native.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_jetboarding_native.cpp

$(BUILD)/test_airportjetspawners_native: tests/test_airportjetspawners_native.cpp tests/test_airportjets.cpp tests/test_planeflight.cpp tests/nativejetshadowarm.h tests/nativeweavearm.h src/airportjets.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_airportjetspawners_native.cpp

$(BUILD)/test_jetcrouchgait: tests/test_jetcrouchgait.cpp tests/test_jetboardingpose.cpp tests/test_jetboarding_assets.cpp tests/test_airportjets.cpp tests/test_planeflight.cpp src/airportjets.cpp src/jetboardingpose.cpp src/body.cpp tools/bake/gamefiles.cpp $(wildcard src/*.h tools/bake/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_jetcrouchgait.cpp tools/bake/gamefiles.cpp -lz

$(BUILD)/test_jetboardingpose: tests/test_jetboardingpose.cpp tests/test_jetboarding_assets.cpp tests/test_airportjets.cpp tests/test_planeflight.cpp src/airportjets.cpp src/jetboardingpose.cpp src/body.cpp tools/bake/gamefiles.cpp $(wildcard src/*.h tools/bake/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_jetboardingpose.cpp tools/bake/gamefiles.cpp -lz

$(BUILD)/test_jetshadow_assets: tests/test_jetshadow_assets.cpp tests/test_airportjetmodel.cpp tests/nativejetshadowarm.h tests/nativeweavearm.h src/airportjetmodel.cpp src/planes.cpp tools/bake/gamefiles.cpp $(wildcard src/*.h tools/bake/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_jetshadow_assets.cpp tools/bake/gamefiles.cpp -lz

$(BUILD)/test_jetshadow_native: tests/test_jetshadow_native.cpp tests/nativejetshadowarm.h tests/nativeweavearm.h src/jetshadow.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_jetshadow_native.cpp

$(BUILD)/test_towercollision: tests/test_towercollision.cpp src/towercollision.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_towercollision.cpp

$(BUILD)/test_towercollision_assets: tests/test_towercollision_assets.cpp $(GAMEFILES) $(wildcard tools/bake/*.h src/*.h) src/mapdata.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_towercollision_assets.cpp $(GAMEFILES) src/mapdata.cpp -lz

$(BUILD)/test_towercollision_native: tests/test_towercollision_native.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_towercollision_native.cpp

$(BUILD)/vcmm-towercollision: tools/bake/towercollision.cpp $(GAMEFILES) $(wildcard tools/bake/*.h) src/mapdata.cpp src/mapdata.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tools/bake/towercollision.cpp $(GAMEFILES) src/mapdata.cpp -lz

$(BUILD)/test_trafficweave: tests/test_trafficweave.cpp tests/nativeweavearm.h src/trafficweave.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_trafficweave.cpp

FEEDBACK_TESTS += test_handlingmodel test_handlinghook test_drivelog test_headbalance
FEEDBACK_TESTS += test_glassmodel test_carglass
FEEDBACK_TESTS += test_partsmodel test_carparts test_dentmodel test_cardeform test_mechanicalmodel test_carmechanics

test-feedback: $(addprefix $(BUILD)/,$(FEEDBACK_TESTS))
	@set -e; for test in $(FEEDBACK_TESTS); do $(BUILD)/$$test; done

test: test-feedback

$(BUILD)/test_ballistics: tests/test_ballistics.cpp src/ballistics.cpp src/ballistics.h src/nativebullet.h src/game.h src/hooks.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_ballistics.cpp

$(BUILD)/test_pedcontacthook: tests/test_pedcontacthook.cpp src/reactions.cpp src/playerreact.cpp src/reactcontact.cpp src/reactphysics.cpp src/reactrig.cpp src/reactself.cpp src/reactimpact.cpp src/playerreactmodel.cpp $(wildcard src/*.h) src/reactcrowd.cpp src/reactcrowd.h src/reactshape.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_pedcontacthook.cpp src/reactphysics.cpp src/reactrig.cpp src/reactself.cpp src/reactimpact.cpp src/playerreactmodel.cpp src/reactcrowd.cpp

$(BUILD)/test_shotcollision: tests/test_shotcollision.cpp src/shotcollision.cpp src/shotcollision.h tools/bake/visualmodel.cpp tools/bake/gamefiles.cpp src/mapdata.cpp $(wildcard src/*.h tools/bake/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_shotcollision.cpp tools/bake/visualmodel.cpp tools/bake/gamefiles.cpp src/mapdata.cpp -lz

$(BUILD)/test_hunterhook: tests/test_hunterhook.cpp src/hunter.cpp src/hunter.h $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_hunterhook.cpp

$(BUILD)/test_flightmodelhook: tests/test_flightmodelhook.cpp src/flightmodel.cpp src/planeflight.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_flightmodelhook.cpp

$(BUILD)/test_flightmodel_hunter: tests/test_flightmodel_hunter.cpp tests/test_flightmodelhook.cpp src/flightmodel.cpp src/planeflight.cpp src/hunter.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_flightmodel_hunter.cpp

$(BUILD)/test_flightownership_native: tests/test_flightownership_native.cpp tests/test_flightmodelhook.cpp src/flightmodel.cpp src/planeflight.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_flightownership_native.cpp

$(BUILD)/test_planeflight_nativecaller: tests/test_planeflight_nativecaller.cpp tests/test_flightmodelhook.cpp src/flightmodel.cpp src/planeflight.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_planeflight_nativecaller.cpp

$(BUILD)/test_planeflight: tests/test_planeflight.cpp src/planeflight.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_planeflight.cpp

$(BUILD)/test_planeflight_roster: tests/test_planeflight_roster.cpp tests/test_planeflight.cpp src/planeflight.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_planeflight_roster.cpp

$(BUILD)/test_airportjets: tests/test_airportjets.cpp tests/test_planeflight.cpp src/airportjets.cpp src/planeflight.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_airportjets.cpp

$(BUILD)/test_aircraftdamage: tests/test_aircraftdamage.cpp src/aircraftdamage.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_aircraftdamage.cpp

$(BUILD)/test_jetlights: tests/test_jetlights.cpp src/jetlights.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_jetlights.cpp

$(BUILD)/test_jetwheel_flight: tests/test_jetwheel_flight.cpp tests/test_planeflight.cpp src/planeflight.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_jetwheel_flight.cpp

$(BUILD)/test_jetwheels: tests/test_jetwheels.cpp src/jetwheels.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_jetwheels.cpp

$(BUILD)/test_jetaudio: tests/test_jetaudio.cpp src/jetaudio.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_jetaudio.cpp

$(BUILD)/test_gridfixhook: tests/test_gridfixhook.cpp src/gridfix.cpp src/gridclip.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_gridfixhook.cpp src/gridclip.cpp

$(BUILD)/test_planestability: tests/test_planestability.cpp tests/test_planeflight.cpp src/planeflight.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_planestability.cpp

$(BUILD)/test_planeengine: tests/test_planeengine.cpp tests/test_planeflight.cpp src/planeflight.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_planeengine.cpp

$(BUILD)/test_planeglide: tests/test_planeglide.cpp tests/test_planeengine.cpp tests/test_planeflight.cpp src/planeflight.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_planeglide.cpp

$(BUILD)/test_airworld: tests/test_airworld.cpp tests/fixtures/airworld_camera_consumer.cpp tests/test_aircraftcamerahook.cpp src/camera.cpp src/headbalance.cpp src/headbalance.h src/airworld.cpp src/canvas.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -Wl,-dead_strip -o $@ tests/test_airworld.cpp tests/fixtures/airworld_camera_consumer.cpp src/canvas.cpp src/headbalance.cpp

$(BUILD)/test_airworld_native: tests/test_airworld_native.cpp $(GAMEFILES) $(wildcard tools/bake/*.h) src/mapdata.cpp src/mapdata.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_airworld_native.cpp $(GAMEFILES) src/mapdata.cpp -lz

$(BUILD)/test_jetwash: tests/test_jetwash.cpp tests/test_airportjets.cpp tests/test_planeflight.cpp tests/test_reactionhook.cpp src/jetwash.cpp src/airportjets.cpp src/planeflight.cpp src/reactions.cpp src/reactphysics.cpp src/reactrig.cpp src/reactcontact.cpp src/reactself.cpp src/reactimpact.cpp $(wildcard src/*.h) src/reactcrowd.cpp src/reactcrowd.h src/reactshape.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_jetwash.cpp src/reactphysics.cpp src/reactrig.cpp src/reactcontact.cpp src/reactself.cpp src/reactimpact.cpp src/reactcrowd.cpp

$(BUILD)/test_jetwash_assets: tests/test_jetwash_assets.cpp tests/test_airworld_native.cpp $(GAMEFILES) $(wildcard tools/bake/*.h) src/mapdata.cpp src/mapdata.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_jetwash_assets.cpp $(GAMEFILES) src/mapdata.cpp -lz

$(BUILD)/test_learjetseat_assets: tests/test_learjetseat_assets.cpp tests/test_aircraftcamerahook.cpp tests/test_damageoverlayhook.cpp src/player.cpp src/camera.cpp src/headbalance.cpp src/headbalance.h src/fpfeedback.cpp src/draw.cpp src/canvas.cpp src/fpbob.cpp src/scope.cpp src/game.cpp tools/bake/gamefiles.cpp $(wildcard src/*.h tools/bake/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -Wl,-dead_strip -o $@ tests/test_learjetseat_assets.cpp src/fpfeedback.cpp src/draw.cpp src/canvas.cpp src/headbalance.cpp src/fpbob.cpp src/scope.cpp src/game.cpp tools/bake/gamefiles.cpp -lz

$(BUILD)/test_jetground: tests/test_jetground.cpp tests/test_jetwheels.cpp src/jetwheels.cpp src/planeflight.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_jetground.cpp

$(BUILD)/test_rotorassets: tests/test_rotorassets.cpp tests/test_shotcollision.cpp src/flightmodel.cpp src/shotcollision.cpp tools/bake/visualmodel.cpp tools/bake/gamefiles.cpp src/mapdata.cpp $(wildcard src/*.h tools/bake/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_rotorassets.cpp tools/bake/visualmodel.cpp tools/bake/gamefiles.cpp src/mapdata.cpp -lz

$(BUILD)/test_planeflight_assets: tests/test_planeflight_assets.cpp tools/bake/gamefiles.cpp tools/bake/gamefiles.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_planeflight_assets.cpp tools/bake/gamefiles.cpp -lz

$(BUILD)/test_flightpath_assets: tests/test_flightpath_assets.cpp src/flightpath.cpp src/flightpath.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_flightpath_assets.cpp src/flightpath.cpp

$(BUILD)/test_fpwalkcontacthook: tests/test_fpwalkcontacthook.cpp tests/test_pedcontacthook.cpp src/fpwalk.cpp src/reactions.cpp src/playerreact.cpp src/fpgait.cpp src/fpmove.cpp src/reactphysics.cpp src/reactrig.cpp src/reactself.cpp src/reactimpact.cpp src/playerreactmodel.cpp $(wildcard src/*.h) src/reactcrowd.cpp src/reactcrowd.h src/reactshape.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_fpwalkcontacthook.cpp src/fpgait.cpp src/fpmove.cpp src/reactphysics.cpp src/reactrig.cpp src/reactself.cpp src/reactimpact.cpp src/playerreactmodel.cpp src/reactcrowd.cpp

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

$(BUILD)/test_vmhands: tests/test_vmhands.cpp src/vmhands.cpp src/vmhands.h src/vmpose.cpp src/vmpose.h src/vmarms.cpp src/vmarms.h src/vmgun.cpp src/vmgun.h src/vmanim.cpp src/vmanim.h src/vmkick.cpp src/vmkick.h src/vmmelee.cpp src/vmmelee.h src/vmskin.h src/vmmath.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_vmhands.cpp src/vmhands.cpp src/vmpose.cpp src/vmarms.cpp src/vmgun.cpp src/vmanim.cpp src/vmkick.cpp src/vmmelee.cpp

$(BUILD)/test_vmanim: tests/test_vmanim.cpp src/vmanim.cpp src/vmanim.h src/vmkick.cpp src/vmkick.h src/vmmelee.cpp src/vmmelee.h src/vmmath.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_vmanim.cpp src/vmanim.cpp src/vmkick.cpp src/vmmelee.cpp

$(BUILD)/test_vmkick: tests/test_vmkick.cpp src/vmkick.cpp src/vmkick.h src/vmmath.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_vmkick.cpp src/vmkick.cpp

$(BUILD)/test_vmmelee: tests/test_vmmelee.cpp src/vmmelee.cpp src/vmmelee.h src/vmmath.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_vmmelee.cpp src/vmmelee.cpp

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

$(BUILD)/test_consolespot: tests/test_consolespot.cpp src/consolespot.cpp src/consolespot.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_consolespot.cpp src/consolespot.cpp

$(BUILD)/test_helizones: tests/test_helizones.cpp src/helizones.cpp src/helizones.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_helizones.cpp src/helizones.cpp

$(BUILD)/test_flightpath: tests/test_flightpath.cpp src/flightpath.cpp src/flightpath.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_flightpath.cpp src/flightpath.cpp

$(BUILD)/test_grip: tests/test_grip.cpp src/grip.cpp src/grip.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_grip.cpp src/grip.cpp

$(BUILD)/test_gridclip: tests/test_gridclip.cpp src/gridclip.cpp src/gridclip.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_gridclip.cpp src/gridclip.cpp

$(BUILD)/test_seatview: tests/test_seatview.cpp src/seatview.cpp src/seatview.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_seatview.cpp src/seatview.cpp

$(BUILD)/test_spotters: tests/test_spotters.cpp src/spotters.cpp src/spotters.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_spotters.cpp src/spotters.cpp

$(BUILD)/test_falldamage: tests/test_falldamage.cpp src/falldamage.cpp src/falldamage.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_falldamage.cpp src/falldamage.cpp

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

test: $(BUILD)/test_estatelighting $(BUILD)/test_gameplaymodel $(BUILD)/test_menulabels $(BUILD)/test_projection $(BUILD)/test_turns $(BUILD)/test_heat $(BUILD)/test_magnet $(BUILD)/test_keys \
      $(BUILD)/test_bindings $(BUILD)/test_hinttext $(BUILD)/test_controlsmodel \
      $(BUILD)/test_inifile $(BUILD)/test_fpmove $(BUILD)/test_fplook $(BUILD)/test_fpbob $(BUILD)/test_fpgait $(BUILD)/test_fontbuf $(BUILD)/test_vmskin $(BUILD)/test_vmarms $(BUILD)/test_vmgun $(BUILD)/test_vmpose $(BUILD)/test_vmview $(BUILD)/test_vmfit $(BUILD)/test_vmanim $(BUILD)/test_vmkick $(BUILD)/test_vmmelee $(BUILD)/test_vmhands $(BUILD)/test_sfxdsp $(BUILD)/test_gunsfx $(BUILD)/test_shotsynth $(BUILD)/test_scope $(BUILD)/test_fpgun $(BUILD)/test_hookname $(BUILD)/test_fpfire $(BUILD)/test_fpcross $(BUILD)/test_consolespot $(BUILD)/test_helizones $(BUILD)/test_flightpath $(BUILD)/test_grip $(BUILD)/test_gridclip $(BUILD)/test_spotters $(BUILD)/test_falldamage $(BUILD)/test_seatview
	$(BUILD)/test_gameplaymodel
	$(BUILD)/test_estatelighting
	$(BUILD)/test_menulabels
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
	$(BUILD)/test_vmkick
	$(BUILD)/test_vmmelee
	$(BUILD)/test_vmhands
	$(BUILD)/test_sfxdsp
	$(BUILD)/test_gunsfx
	$(BUILD)/test_shotsynth
	$(BUILD)/test_scope
	$(BUILD)/test_fpgun
	$(BUILD)/test_hookname
	$(BUILD)/test_fpfire
	$(BUILD)/test_fpcross
	$(BUILD)/test_consolespot
	$(BUILD)/test_helizones
	$(BUILD)/test_flightpath
	$(BUILD)/test_grip
	$(BUILD)/test_gridclip
	$(BUILD)/test_spotters
	$(BUILD)/test_falldamage
	$(BUILD)/test_seatview

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

$(BUILD)/test_dentmodel: tests/test_dentmodel.cpp src/dentmodel.cpp src/dentmodel.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_dentmodel.cpp src/dentmodel.cpp

$(BUILD)/test_cardeform: tests/test_cardeform.cpp src/carimpact.cpp src/dentmodel.cpp src/partsmodel.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_cardeform.cpp src/dentmodel.cpp src/partsmodel.cpp

$(BUILD)/test_mechanicalmodel: tests/test_mechanicalmodel.cpp src/mechanicalmodel.cpp src/mechanicalmodel.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_mechanicalmodel.cpp src/mechanicalmodel.cpp

$(BUILD)/test_carmechanics: tests/test_carmechanics.cpp src/carmechanics.cpp src/mechanicalmodel.cpp src/partsmodel.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_carmechanics.cpp src/mechanicalmodel.cpp src/partsmodel.cpp

$(BUILD)/test_mechanical_assets: tests/test_mechanical_assets.cpp src/mechanicalmodel.cpp tools/bake/gamefiles.cpp $(wildcard src/*.h tools/bake/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_mechanical_assets.cpp src/mechanicalmodel.cpp tools/bake/gamefiles.cpp -lz

$(BUILD)/test_mechanical_native: tests/test_mechanical_native.cpp tests/test_carmechanics.cpp tests/nativejetshadowarm.h tests/nativeweavearm.h src/carmechanics.cpp src/mechanicalmodel.cpp src/partsmodel.cpp $(wildcard src/*.h) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test_mechanical_native.cpp src/mechanicalmodel.cpp src/partsmodel.cpp
