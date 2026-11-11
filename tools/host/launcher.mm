#import <Cocoa/Cocoa.h>
#import <ApplicationServices/ApplicationServices.h>
#include <mach-o/dyld.h>
#include <filesystem>
#include <string>
#include <vector>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

int main(int argc,char **argv){
 @autoreleasepool {
  uint32_t size=4096;std::vector<char> path(size);if(_NSGetExecutablePath(path.data(),&size))return 2;
  auto contents=std::filesystem::canonical(path.data()).parent_path().parent_path();bool lab=false,chooser=false,check=false;std::string screenshot;std::vector<std::string> forwarded;
  for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--physics-test")lab=true;else if(arg=="--choose-mode")chooser=true;else if(arg=="--check-launcher")check=true;else if(arg=="--lab-smoke"&&i+1<argc){lab=true;screenshot=argv[++i];}else forwarded.push_back(arg);}
  if(!check&&!lab&&(chooser||(CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState)&kCGEventFlagMaskAlternate))){
   [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];[NSApp activateIgnoringOtherApps:YES];
   NSAlert *alert=[NSAlert new];alert.messageText=@"Grand Theft Auto Vice City";alert.informativeText=@"Choose normal gameplay or the experimental aircraft / building strike.";[alert addButtonWithTitle:@"Play Vice City"];[alert addButtonWithTitle:@"Aircraft / building strike"];[alert addButtonWithTitle:@"Cancel"];
   auto response=[alert runModal];if(response==NSAlertThirdButtonReturn)return 0;lab=response==NSAlertSecondButtonReturn;
  }
  auto binary=contents/"MacOS"/(lab?"vcPhysicsHost":"macVC");std::vector<std::string> arguments{binary.string()};
  if(lab){arguments.push_back("--wing-lab");arguments.push_back((contents/"Resources/PhysicsTest/aircraft.txt").string());if(!screenshot.empty()){arguments.push_back("--smoke");arguments.push_back(screenshot);}}
  else for(auto &arg:forwarded)arguments.push_back(arg);
  if(check){for(auto &arg:arguments)std::printf("%s\n",arg.c_str());return 0;}
  if(!std::filesystem::exists(binary)||(lab&&!std::filesystem::exists(contents/"Resources/PhysicsTest/aircraft.txt"))){std::fprintf(stderr,"Required game/test files are missing. Restore the saved app or reinstall the test package.\n");return 2;}
  if(lab){auto logs=std::filesystem::path(NSHomeDirectory().UTF8String)/"Library/Logs";std::filesystem::create_directories(logs);int fd=open((logs/"VCPhysicsTest.log").c_str(),O_CREAT|O_WRONLY|O_APPEND,0600);if(fd>=0){dup2(fd,STDOUT_FILENO);dup2(fd,STDERR_FILENO);close(fd);}}
  std::vector<char*> raw;for(auto &arg:arguments)raw.push_back(arg.data());raw.push_back(nullptr);execv(binary.c_str(),raw.data());perror("Starting Vice City");return 2;
 }
}
