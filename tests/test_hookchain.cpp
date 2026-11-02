#include <dlfcn.h>
static void *HookSymbol(void *,const char *);
#define dlsym HookSymbol
#include "../src/hooks.cpp"
#undef dlsym
#include <array>
#include <cstdio>
#include <cstring>
void Log(const char *,...) {}
static void *gExported;
static void *HookSymbol(void *,const char *name){return !std::strcmp(name,"exported_fixture")?gExported:nullptr;}
int main()
{
	alignas(16) std::array<uint8_t,512> bytes{};
	auto *table=reinterpret_cast<hooks::Header *>(bytes.data()+64);
	table->version=2;table->count=1;
	auto *entry=reinterpret_cast<hooks::Entry *>(table+1);
	*entry={hooks::NameHash("fixture"),160,256,208,0};
	// The native entry's patched B targets a stub whose trampoline starts+16.
	*reinterpret_cast<uint32_t *>(bytes.data()+160)=0x14000000u+(192-160)/4;
	hooks::gImage=reinterpret_cast<const mach_header_64 *>(bytes.data());hooks::gTable=table;
	auto **slot=reinterpret_cast<void **>(bytes.data()+256);
	void *first=bytes.data()+320,*second=bytes.data()+352,*original=bytes.data()+208;
	int fail=0;auto check=[&](bool ok,const char *s){if(!ok){printf("FAIL: %s\n",s);++fail;}};
	check(hooks::Chain("fixture",first)==original && *slot==first,"empty slot chains through native trampoline");
	check(hooks::Chain("fixture",second)==first && *slot==second,"occupied slot preserves previous feature handler");
	check(!hooks::Chain("fixture",second) && *slot==second,"self-chain leaves slot unchanged");
	check(!hooks::Chain("fixture",nullptr) && *slot==second,"null replacement leaves slot unchanged");
	check(!hooks::Chain("absent",first) && *slot==second,"missing entry leaves slot unchanged");
	// Local native functions are in the signed patch table but are absent from
	// dyld's exported symbols (as the helicopter RT-candidate emitter is).
	auto call=[&](int target){*reinterpret_cast<uint32_t *>(bytes.data()+300)=0x94000000u|(uint32_t((target-300)/4)&0x03ffffffu);};
	call(160);
	check(hooks::IsCallTo(304,"fixture"),"private patched target verifies without dlsym export");
	call(164);check(!hooks::IsCallTo(304,"fixture"),"wrong private call target is rejected");
	*reinterpret_cast<uint32_t *>(bytes.data()+300)=0x14000000u|((160-300)/4&0x03ffffffu);
	check(!hooks::IsCallTo(304,"fixture"),"a jump cannot verify as a call");
	call(160);entry->kind=1;
	check(!hooks::IsCallTo(304,"fixture"),"skip entries do not identify private function calls");entry->kind=0;
	const auto *savedTable=hooks::gTable;hooks::gTable=nullptr;gExported=bytes.data()+352;call(352);
	check(hooks::IsCallTo(304,"exported_fixture"),"exported targets retain verified instruction fallback without a table");
	check(!hooks::IsCallTo(304,"fixture"),"an unavailable private target stays unverified");
	hooks::gTable=savedTable;gExported=nullptr;call(160);
	entry->kind=1;check(!hooks::Chain("fixture",first) && *slot==second,"skip patch cannot be chained");entry->kind=0;
	*reinterpret_cast<uint32_t *>(bytes.data()+160)=0;
	check(!hooks::Chain("fixture",first) && *slot==second,"unverified branch cannot be chained");
	check(!hooks::IsCallTo(304,"fixture"),"unpatched table target cannot authorize a private call");
	return fail?1:(puts("hookchain: all passed"),0);
}
