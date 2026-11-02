// The executable owns rendering and disk settings; only those boundaries are replaced.
// Notification delivery and the plugin's load/save/recreation handlers are real.
#include "../src/windowmode.mm"
#include "inifile.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <string>

@interface TestModeWindow : NSObject
@property NSUInteger styleMask;
@end
@implementation TestModeWindow
@end

namespace game { Api api; }
void Log(const char *, ...) {}
namespace hooks {
void *Install(const char *, void *) { return nullptr; }
const void *ImageBase() { return nullptr; }
}
namespace {
std::string ini;
bool nativeWant, loadOK = true, recreateOK = true;
bool closing = false;
int windowed = 1, saves = 0;
__strong TestModeWindow *current;
std::array<uint8_t, 64> globals{}, platform{};

int Value(const char *key, int fallback)
{
 const std::string prefix = std::string(key) + "=";
 const size_t p = ini.find(prefix);
 return p == std::string::npos ? fallback : std::stoi(ini.substr(p + prefix.size()));
}
bool Read(const char *section, const char *key, int *out)
{
 if (std::strcmp(section,"VideoMode")) return false;
 if (ini.find(std::string(key)+"=")==std::string::npos) return false;
 *out = Value(key,*out);
 return true;
}
void Store(const char *section, const char *key, int value)
{
 ini = inifile::WithValue(ini,section,key,std::to_string(value));
}
bool NativeLoad()
{
 if (loadOK) windowed=Value("Windowed",1);
 return loadOK;
}
void NativeSave()
{
 Store("VideoMode","Windowed",windowed);
 Store("VideoMode","Width",1920);
 ++saves;
}
void *Cocoa(void *) { return (__bridge void *)current; }
int ShouldClose(void *) { return closing ? 1 : 0; }
void Notify(NSNotificationName name, TestModeWindow *window)
{
 [[NSNotificationCenter defaultCenter] postNotificationName:name object:window];
}
bool NativeSetVM(int, int)
{
 // The native recreation path exits the old fullscreen Space before replacing it.
 current.styleMask=0;
 Notify(NSWindowDidExitFullScreenNotification,current);
 if (!recreateOK) return false;
 current=[TestModeWindow new];
 current.styleMask=nativeWant ? NSWindowStyleMaskFullScreen : 0;
 return true;
}
void Reset(bool defaultFullscreen, std::string settings)
{
 ini=std::move(settings); nativeWant=defaultFullscreen; loadOK=true; recreateOK=true; saves=0; closing=false;
 current=[TestModeWindow new]; current.styleMask=0;
 globals={}; platform={};
 *reinterpret_cast<uint8_t **>(globals.data()+0x20)=platform.data();
 *reinterpret_cast<void **>(platform.data())=reinterpret_cast<void *>(0x1234);
 game::api.rsGlobal=globals.data(); game::api.SaveINISettings=windowmode::Save;
 windowmode::gWantNative=&nativeWant;
 windowmode::gReadIni=Read; windowmode::gStoreIni=Store;
 windowmode::gCocoaWindow=Cocoa;
 windowmode::gShouldClose=ShouldClose;
 windowmode::origLoad=NativeLoad; windowmode::origSave=NativeSave; windowmode::origSetVM=NativeSetVM;
 windowmode::gNativeFullscreen=defaultFullscreen;
 windowmode::gChangingVideo=0;
}
}
int main()
{
 @autoreleasepool {
 int failed=0;
 auto check=[&](bool yes,const char *what){if(!yes){std::printf("FAIL: %s\n",what);++failed;}};
 windowmode::Observe();
 Reset(true,"[VideoMode]\nWindowed=1\nWidth=1920\nNativeFullscreen=0\n");
 check(windowmode::Load() && !nativeWant,"a saved window exits the native fullscreen startup default");
 check(windowed==1,"restoring a window preserves the GLFW video mode");
 Reset(false,"[VideoMode]\nWindowed=1\nNativeFullscreen=1\n");
 check(windowmode::Load() && nativeWant,"saved native fullscreen is restored before window creation");
 Reset(true,"[VideoMode]\nWindowed=1\n");
 check(windowmode::Load() && nativeWant,"old profiles keep the native default when the key is absent");
 ini="[VideoMode]\nWindowed=1\nNativeFullscreen=9\n";
 check(windowmode::Load() && nativeWant,"invalid saved modes do not replace the native default");
 loadOK=false; nativeWant=false;
 check(!windowmode::Load() && !nativeWant,"a failed native load is forwarded without applying stale settings");

 Reset(true,"[VideoMode]\nWindowed=1\nNativeFullscreen=1\n[Audio]\nMusicVolume=64\n");
 windowmode::Load();
 current.styleMask=NSWindowStyleMaskFullScreen;
 Notify(NSWindowDidEnterFullScreenNotification,current);
 current.styleMask=0;
 Notify(NSWindowDidExitFullScreenNotification,current);
 check(Value("NativeFullscreen",-1)==0,"exiting with the Mac window button persists a window immediately");
 check(Value("Windowed",-1)==1,"Mac window controls do not select exclusive GLFW fullscreen");
 check(Value("MusicVolume",-1)==64,"window-mode saves preserve unrelated native preferences");
 nativeWant=true; windowmode::Load();
 check(!nativeWant,"the next launch reopens the last windowed choice");
 current.styleMask=NSWindowStyleMaskFullScreen;
 Notify(NSWindowDidEnterFullScreenNotification,current);
 check(Value("NativeFullscreen",-1)==1,"entering native fullscreen persists immediately");
 nativeWant=false; windowmode::Load();
 check(nativeWant,"the next launch reopens the last fullscreen choice");

 const int saved=saves;
 TestModeWindow *other=[TestModeWindow new];
 Notify(NSWindowDidExitFullScreenNotification,other);
 check(saves==saved && Value("NativeFullscreen",-1)==1,"unrelated windows cannot overwrite the game mode");
 current.styleMask=0;
 *reinterpret_cast<int32_t *>(globals.data()+0x1c)=1;
 Notify(NSWindowDidExitFullScreenNotification,current);
 check(saves==saved && Value("NativeFullscreen",-1)==1,"menu-quit window destruction cannot overwrite the last fullscreen choice");
 *reinterpret_cast<int32_t *>(globals.data()+0x1c)=0;
 closing=true;
 Notify(NSWindowDidExitFullScreenNotification,current);
 check(saves==saved && Value("NativeFullscreen",-1)==1,"OS-close destruction cannot overwrite the last fullscreen choice");
 closing=false;
 current.styleMask=NSWindowStyleMaskFullScreen;
 TestModeWindow *old=current;
 check(windowmode::SetVideoMode(0,3),"successful native resolution changes return success");
 check(Value("NativeFullscreen",-1)==1,"temporary fullscreen exits during renderer recreation are ignored");
 Notify(NSWindowDidExitFullScreenNotification,old);
 check(Value("NativeFullscreen",-1)==1,"late notifications from a destroyed window are ignored");
 current.styleMask=0;
 Notify(NSWindowDidExitFullScreenNotification,current);
 check(Value("NativeFullscreen",-1)==0,"the replacement window remains observed");
 recreateOK=false;
 check(!windowmode::SetVideoMode(0,4) && !windowmode::gChangingVideo,"failed native changes release notification suppression");
 windowmode::Save();
 check(Value("NativeFullscreen",-1)==0 && Value("Width",0)==1920,"ordinary native saves keep both mode and native settings");

 // Actual 1.0.5217 ADRP/LDRB/CMP/B.NE at psPostRWinit+688, relocated as dyld does.
 alignas(4096) static std::array<uint8_t,0x140000> image{};
 auto *post=image.data()+0xf18;
 const uint32_t instructions[]={0xf0000988,0x397c4108,0x7100051f,0x54000421};
 std::memcpy(post+688,instructions,sizeof instructions);
 check(windowmode::NativeFlag(post)==reinterpret_cast<bool *>(image.data()+0x134f10),"the native flag is decoded from the verified instruction pair");
 post[688]=0;
 check(!windowmode::NativeFlag(post),"unexpected native instructions cannot yield a writable private pointer");
 check(!windowmode::NativeFlag(nullptr),"missing native post-init symbols fail closed");
 return failed?1:(std::puts("windowmode: all passed"),0);
 }
}
