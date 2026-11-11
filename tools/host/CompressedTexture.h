#pragma once
#include <cstdint>
inline uint32_t CompressedTextureLevelSize(uint32_t width,uint32_t height,unsigned level,uint32_t blockBytes){
 if(!width||!height)return 0;
 for(unsigned i=0;i<level;++i){if(width>1)width/=2;if(height>1)height/=2;}
 return ((width+3)/4)*((height+3)/4)*blockBytes;
}
