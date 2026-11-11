#pragma once
#include <algorithm>
#include <cstddef>
#include <cstring>
inline int CopyControllerButtons(unsigned char *destination,std::size_t capacity,const unsigned char *source,int count){
 std::memset(destination,0,capacity);
 if(!source||count<=0)return 0;
 auto size=std::min(capacity,std::size_t(count));std::memcpy(destination,source,size);return int(size);
}
