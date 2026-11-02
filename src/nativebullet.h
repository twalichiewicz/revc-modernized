#pragma once
#include "types.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

// macVC 1.0.5217 CBulletInfo::AddBullet/Update, verified native storage.
namespace nativebullet {
constexpr size_t kCount=100,kStride=0x38;
struct Record {
	int32_t type;
	uint32_t reserved;
	uint8_t *creator;
	float deadline;
	uint8_t active,padding[3];
	Vec3 position,velocity;
	uint16_t damage;
	uint8_t trailing[6];
};
static_assert(sizeof(Record)==kStride && offsetof(Record,creator)==8 &&
    offsetof(Record,deadline)==0x10 && offsetof(Record,active)==0x14 &&
    offsetof(Record,position)==0x18 && offsetof(Record,velocity)==0x24 && offsetof(Record,damage)==0x30);
inline Record Read(const uint8_t *base,size_t index)
{
	Record record;std::memcpy(&record,base+index*kStride,sizeof record);return record;
}
} // namespace nativebullet
