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
 @autoreleasepool{
  uint32_t capacity=4096;std::vector<char> path(capacity);if(_NSGetExecutablePath(path.data(),&capacity))return 2;
  auto contents=std::filesystem::canonical(path.data()).parent_path().parent_path();bool previous=false,choose=false,check=false;std::vector<std::string> forwarded;
  for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--legacy-game")previous=true;else if(arg=="--choose-mode")choose=true;else if(arg=="--check-launcher")check=true;else forwarded.push_back(arg);}
  if(!check&&(choose||(CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState)&kCGEventFlagMaskAlternate))){
   [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];[NSApp activateIgnoringOtherApps:YES];NSAlert *alert=[NSAlert new];alert.messageText=@"Grand Theft Auto Vice City";alert.informativeText=@"Choose a game build.";[alert addButtonWithTitle:@"Play Vice City"];[alert addButtonWithTitle:@"Previous build"];[alert addButtonWithTitle:@"Cancel"];auto response=[alert runModal];if(response==NSAlertThirdButtonReturn)return 0;previous=response==NSAlertSecondButtonReturn;
  }
  auto binary=contents/"MacOS"/(previous?"macVC":"vcGameplayHost");std::vector<std::string> arguments{binary.string()};arguments.insert(arguments.end(),forwarded.begin(),forwarded.end());
  if(check){for(auto &arg:arguments)std::printf("%s\n",arg.c_str());return 0;}
  if(!std::filesystem::exists(binary)){std::fprintf(stderr,"Vice City executable is missing. Restore the saved app bundle.\n");return 2;}
  if(!previous){auto directory=std::filesystem::path(NSHomeDirectory().UTF8String)/"Library/Logs";std::filesystem::create_directories(directory);auto log=directory/"VCGameplay.log";std::error_code error;if(std::filesystem::exists(log)&&std::filesystem::file_size(log,error)>10*1024*1024)std::filesystem::rename(log,directory/"VCGameplay.previous.log",error);int fd=open(log.c_str(),O_CREAT|O_WRONLY|O_APPEND,0600);if(fd>=0){dup2(fd,STDOUT_FILENO);dup2(fd,STDERR_FILENO);close(fd);}}
  std::vector<char*> raw;for(auto &arg:arguments)raw.push_back(arg.data());raw.push_back(nullptr);execv(binary.c_str(),raw.data());perror("Starting Vice City");return 2;
 }
}
