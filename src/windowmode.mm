#include "windowmode.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#import <AppKit/AppKit.h>
#include <cstring>
#include <dlfcn.h>

namespace windowmode {
namespace {
bool (*origLoad)();
void (*origSave)();
bool (*origSetVM)(int,int);
bool (*gReadIni)(const char *,const char *,int *);
void (*gStoreIni)(const char *,const char *,int);
void *(*gCocoaWindow)(void *);
int (*gShouldClose)(void *);
bool *gWantNative;
int gNativeFullscreen, gChangingVideo;
id gEnterObserver, gExitObserver;

bool *NativeFlag(const uint8_t *postInit)
{
	if (!postInit)
		return nullptr;
	// macVC 1.0.5217: psPostRWinit+688 reads wantNativeFullScreen, then
	// enters the Mac fullscreen Space when the RW mode is windowed. Resolve
	// the private byte through its verified ARM64 instructions, with ASLR.
	uint32_t words[4];
	std::memcpy(words, postInit + 688, sizeof words);
	if (words[0] != 0xf0000988 || words[1] != 0x397c4108 ||
	    words[2] != 0x7100051f || words[3] != 0x54000421)
		return nullptr;
	intptr_t pages = (((words[0] >> 5) & 0x7ffff) << 2) | ((words[0] >> 29) & 3);
	if (pages & (1 << 20))
		pages -= 1 << 21;
	const uintptr_t page = reinterpret_cast<uintptr_t>(postInit + 688) & ~uintptr_t(4095);
	return reinterpret_cast<bool *>(page + pages * 4096 + ((words[1] >> 10) & 4095));
}

void *CurrentGlfwWindow()
{
	if (!game::api.rsGlobal)
		return nullptr;
	auto *platform = *reinterpret_cast<uint8_t **>(game::api.rsGlobal + 0x20);
	return platform ? *reinterpret_cast<void **>(platform) : nullptr;
}

bool Load()
{
	const bool loaded = origLoad();
	if (loaded) {
		// Preserve the native default (fullscreen) for existing profiles. A
		// missing or invalid new key never changes the renderer's Windowed key.
		int fullscreen = *gWantNative ? 1 : 0;
		gReadIni("VideoMode", "NativeFullscreen", &fullscreen);
		if (fullscreen == 0 || fullscreen == 1)
			*gWantNative = fullscreen != 0;
		gNativeFullscreen = *gWantNative ? 1 : 0;
	}
	return loaded;
}

void Save()
{
	// Use the game's own INI map/writer so later native saves keep this key.
	gStoreIni("VideoMode", "NativeFullscreen", gNativeFullscreen);
	origSave();
}

bool SetVideoMode(int subsystem, int mode)
{
	// Native resolution changes temporarily exit fullscreen and destroy the
	// window. Those intermediate notifications are not a user preference.
	struct Changing {
		Changing() { ++gChangingVideo; }
		~Changing() { --gChangingVideo; }
	} changing;
	return origSetVM(subsystem, mode);
}

void Completed(NSNotification *note)
{
	if (gChangingVideo || !game::api.rsGlobal ||
	    *reinterpret_cast<const int32_t *>(game::api.rsGlobal + 0x1c))
		return;
	void *glfwWindow = CurrentGlfwWindow();
	if (!glfwWindow || gShouldClose(glfwWindow))
		return;
	NSWindow *window = (__bridge NSWindow *)gCocoaWindow(glfwWindow);
	if (!window || note.object != window)
		return;
	gNativeFullscreen = (window.styleMask & NSWindowStyleMaskFullScreen) ? 1 : 0;
	*gWantNative = gNativeFullscreen != 0;
	Save();
}

void Observe()
{
	if (gEnterObserver)
		return;
	NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
	gEnterObserver = [center addObserverForName:NSWindowDidEnterFullScreenNotification object:nil queue:nil
	    usingBlock:^(NSNotification *note) { Completed(note); }];
	gExitObserver = [center addObserverForName:NSWindowDidExitFullScreenNotification object:nil queue:nil
	    usingBlock:^(NSNotification *note) { Completed(note); }];
}
}

bool Install()
{
	if (origSetVM)
		return true;
	gReadIni = reinterpret_cast<decltype(gReadIni)>(dlsym(RTLD_MAIN_ONLY, "_Z15ReadIniIfExistsPKcS0_Pi"));
	gStoreIni = reinterpret_cast<decltype(gStoreIni)>(dlsym(RTLD_MAIN_ONLY, "_Z8StoreIniPKcS0_i"));
	gCocoaWindow = reinterpret_cast<decltype(gCocoaWindow)>(dlsym(RTLD_DEFAULT, "glfwGetCocoaWindow"));
	gShouldClose = reinterpret_cast<decltype(gShouldClose)>(dlsym(RTLD_DEFAULT, "glfwWindowShouldClose"));
	gWantNative = NativeFlag(static_cast<const uint8_t *>(dlsym(RTLD_MAIN_ONLY, "_Z12psPostRWinitv")));
	if (!gReadIni || !gStoreIni || !gCocoaWindow || !gShouldClose || !gWantNative)
		return false;
	gNativeFullscreen = *gWantNative ? 1 : 0;
	if (!hooks::Install("_Z15LoadINISettingsv", Load, origLoad))
		return false;
	if (!hooks::Install("_Z15SaveINISettingsv", Save, origSave)) {
		hooks::Install("_Z15LoadINISettingsv", reinterpret_cast<void *>(origLoad));
		origLoad = nullptr;
		return false;
	}
	if (!hooks::Install("_psSetVideoMode", SetVideoMode, origSetVM)) {
		hooks::Install("_Z15LoadINISettingsv", reinterpret_cast<void *>(origLoad));
		hooks::Install("_Z15SaveINISettingsv", reinterpret_cast<void *>(origSave));
		origLoad = nullptr;
		origSave = nullptr;
		return false;
	}
	Observe();
	Log("window mode: native fullscreen preference persistence installed");
	return true;
}
}
