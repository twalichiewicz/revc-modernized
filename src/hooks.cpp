#include "hooks.h"
#include "log.h"

#include <crt_externs.h>
#include <cstring>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <mach-o/loader.h>

namespace hooks {
namespace {

constexpr char kMagic[8] = {'V', 'C', 'M', 'M', 'H', 'K', '0', '1'};
constexpr int kNameLen = 96;
constexpr uint32_t kKindHook = 0, kKindSkip = 1;

struct Entry {
	char name[kNameLen];
	uint32_t funcOff, slotOff, origOff, kind;
};
struct Header {
	char magic[8];
	uint32_t version, count;
	uint8_t uuid[16];
};

const mach_header_64 *gImage;
const Header *gTable;

const uint8_t *ImageUuid(const mach_header_64 *mh)
{
	auto *lc = reinterpret_cast<const load_command *>(mh + 1);
	for (uint32_t i = 0; i < mh->ncmds; i++) {
		if (lc->cmd == LC_UUID)
			return reinterpret_cast<const uuid_command *>(lc)->uuid;
		lc = reinterpret_cast<const load_command *>(reinterpret_cast<const uint8_t *>(lc) + lc->cmdsize);
	}
	return nullptr;
}

// Decodes the `b` written over the function's first instruction.
const uint8_t *BranchTarget(const uint8_t *pc)
{
	uint32_t insn;
	memcpy(&insn, pc, 4);
	if ((insn & 0xFC000000) != 0x14000000)
		return nullptr;
	int32_t imm = static_cast<int32_t>(insn << 6) >> 6;
	return pc + static_cast<intptr_t>(imm) * 4;
}

} // namespace

bool Init(const uint8_t expectedUuid[16])
{
	// Not _dyld_get_image_header(0): libraries injected with DYLD_INSERT_LIBRARIES
	// (Steam's overlay) come before the main executable in the image list.
	gImage = reinterpret_cast<const mach_header_64 *>(_NSGetMachExecuteHeader());
	if (!gImage || gImage->magic != MH_MAGIC_64) {
		Log("hooks: main image header not found");
		return false;
	}
	const uint8_t *uuid = ImageUuid(gImage);
	if (expectedUuid && (!uuid || memcmp(uuid, expectedUuid, 16) != 0)) {
		Dl_info info{};
		dladdr(gImage, &info);
		Log("hooks: %s is not the build this plugin was made for; staying vanilla",
		    info.dli_fname ? info.dli_fname : "the main executable");
		return false;
	}
	unsigned long textSize = 0;
	const uint8_t *text = getsectiondata(gImage, "__TEXT", "__text", &textSize);
	const uint8_t *p = reinterpret_cast<const uint8_t *>(gImage + 1) + gImage->sizeofcmds;
	for (; text && p + sizeof(Header) <= text; p += 4) {
		if (memcmp(p, kMagic, sizeof kMagic) == 0) {
			gTable = reinterpret_cast<const Header *>(p);
			break;
		}
	}
	if (!gTable || gTable->version != 1) {
		Log("hooks: patch table not found (run install.sh)");
		gTable = nullptr;
		return false;
	}
	Log("hooks: found table with %u hooks", gTable->count);
	return true;
}

// The table entry for `name`, once its patched branch checks out.
const Entry *Find(const char *name)
{
	if (!gTable)
		return nullptr;
	auto *base = reinterpret_cast<const uint8_t *>(gImage);
	auto *entries = reinterpret_cast<const Entry *>(gTable + 1);
	for (uint32_t i = 0; i < gTable->count; i++) {
		const Entry &e = entries[i];
		if (strncmp(e.name, name, kNameLen) != 0)
			continue;
		const uint8_t *stub = BranchTarget(base + e.funcOff);
		if (!stub || stub + 16 != base + e.origOff) {
			Log("hooks: %s is not patched as expected", name);
			return nullptr;
		}
		return &e;
	}
	Log("hooks: %s missing from patch table", name);
	return nullptr;
}

void *Install(const char *mangledName, void *replacement)
{
	const Entry *e = Find(mangledName);
	if (!e || e->kind != kKindHook)
		return nullptr;
	auto *base = reinterpret_cast<uint8_t *>(const_cast<mach_header_64 *>(gImage));
	*reinterpret_cast<void **>(base + e->slotOff) = replacement;
	return base + e->origOff;
}

volatile uint64_t *Flag(const char *name)
{
	const Entry *e = Find(name);
	if (!e || e->kind != kKindSkip)
		return nullptr;
	auto *base = reinterpret_cast<uint8_t *>(const_cast<mach_header_64 *>(gImage));
	return reinterpret_cast<volatile uint64_t *>(base + e->slotOff);
}

const void *ImageBase() { return gImage; }

bool IsCallTo(uint32_t retOffset, const char *mangledName)
{
	const auto *base = reinterpret_cast<const uint8_t *>(gImage);
	const auto *fn = static_cast<const uint8_t *>(dlsym(RTLD_MAIN_ONLY, mangledName));
	if (!base || !fn)
		return false;
	uint32_t insn;
	memcpy(&insn, base + retOffset - 4, 4);
	const int32_t imm = int32_t(insn << 6) >> 6;
	return (insn & 0xFC000000) == 0x94000000 && base + retOffset - 4 + intptr_t(imm) * 4 == fn;
}

void UninstallAll()
{
	if (!gTable)
		return;
	auto *base = reinterpret_cast<uint8_t *>(const_cast<mach_header_64 *>(gImage));
	auto *entries = reinterpret_cast<const Entry *>(gTable + 1);
	for (uint32_t i = 0; i < gTable->count; i++)
		*reinterpret_cast<void **>(base + entries[i].slotOff) = nullptr;
}

} // namespace hooks
