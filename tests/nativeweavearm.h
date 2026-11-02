#pragma once
// Executes the pristine arm64 weave consumer in this test process. Only its
// external bindings and a pass counter are relocated. The counter diverts a
// nonterminating outer loop to the native epilogue after 64 passes.
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <libkern/OSCacheControl.h>
#include <mach-o/loader.h>
#include <pthread.h>
#include <string>
#include <sys/mman.h>
#include <vector>

namespace nativeweavetest {
inline void Require(bool yes, const char *message)
{
 if (!yes) { std::fprintf(stderr, "native weave fixture: %s\n", message); std::exit(2); }
}
struct Image {
 std::vector<uint8_t> bytes;
 explicit Image(bool load = true) {
  if (!load) return;
  const char *path = std::getenv("MACVC_NATIVE"), *home = std::getenv("HOME");
  const std::string fallback = std::string(home ? home : "") + "/Library/Application Support/macVC/vcmodernhud/macVC.vanilla";
  std::ifstream in(path ? path : fallback.c_str(), std::ios::binary | std::ios::ate);
  Require(bool(in), "open pristine executable (MACVC_NATIVE overrides)");
  const auto size = in.tellg(); Require(size > 0 && size < 128 * 1024 * 1024, "bounded native image");
  bytes.resize(size_t(size)); in.seekg(0); in.read(reinterpret_cast<char *>(bytes.data()), size);
  Require(bool(in), "read native image");
 }
 template<class T> T At(size_t offset) const {
  Require(offset <= bytes.size() && sizeof(T) <= bytes.size() - offset, "file-backed native read");
  T value; std::memcpy(&value, bytes.data() + offset, sizeof value); return value;
 }
 uint32_t Word(uint64_t address) const {
  const auto header = At<mach_header_64>(0); Require(header.magic == MH_MAGIC_64, "thin Mach-O64");
  size_t at = sizeof header;
  for (uint32_t i = 0; i < header.ncmds; ++i) {
   const auto command = At<load_command>(at); Require(command.cmdsize >= 8, "valid command");
   if (command.cmd == LC_SEGMENT_64) {
    const auto segment = At<segment_command_64>(at);
    if (address >= segment.vmaddr && address - segment.vmaddr + 4 <= segment.filesize)
     return At<uint32_t>(size_t(segment.fileoff + address - segment.vmaddr));
   }
   at += command.cmdsize;
  }
  Require(false, "native instruction is mapped"); return 0;
 }
};
inline int64_t Signed(uint32_t value, int bits) { return int32_t(value << (32 - bits)) >> (32 - bits); }
using Weave = float (*)(uint8_t *, uint8_t *, float, float);
struct Node { uint8_t *entity; Node *previous = nullptr, *next = nullptr; };
static_assert(sizeof(Node) == 24);
struct Sector { Node *lists[10]{}; };
static_assert(sizeof(Sector) == 80);
struct Fixture;
inline Fixture *active;
struct Fixture {
 static constexpr uint64_t kStart = 0x100057030, kEnd = 0x100058fb4;
 Image image;
 alignas(4096) std::array<Sector, 80 * 80> sectors{};
 alignas(4096) std::array<uint8_t, 4096> scanPage{};
 alignas(4096) std::array<uint8_t, 65536> modelPage{};
 alignas(16) uint8_t vehicle[0x800]{}, obstacle[0x800]{}, model[0x40]{}, collision[0x40]{};
 uint32_t passes = 0, helperCalls = 0;
 bool poisonHelper = false;
 bool oscillatingHelper = false;
 bool useNativeHelpers = false;
 uint8_t *code = nullptr;
 size_t cursor = 0x3000;
 Weave run = nullptr;
 static void Unused() { Require(false, "unexpected external native dependency"); }
 static void Assert(const char *, const char *, unsigned, const char *) {}
 static void Clear() { std::memset(active->obstacle + 0x66, 0, 2); }
 static void OtherCar(uint8_t *, uint8_t *, float *left, float *right) {
  ++active->helperCalls;
  *left = active->poisonHelper ? NAN : -0.5f;
  *right = active->poisonHelper ? NAN : 0.5f;
  if (active->oscillatingHelper) {
   *left = active->helperCalls & 1 ? -0.5f : -0.25f;
   *right = -*left; Store(active->obstacle + 0x66, uint16_t(0));
  }
 }
 static void *Player() { return nullptr; }
 struct Vec { float x, y, z; };
 static Vec Centre(uint8_t *entity) { Vec out; std::memcpy(&out, entity + 0x38, sizeof out); return out; }
 static float Atan(float y, float x) { return std::atan2(y, x); }
 struct Trig { float sine, cosine; };
 static Trig SinCos(float angle) { return {std::sin(angle), std::cos(angle)}; }
 template<class T> static void Store(uint8_t *at, T value) { std::memcpy(at, &value, sizeof value); }
 void Emit(size_t at, uint32_t value) { Store(code + at, value); }
 uint32_t Branch(size_t from, size_t to, uint32_t op = 0x14000000) {
  return op | (uint32_t((int64_t(to) - int64_t(from)) / 4) & 0x3ffffff);
 }
 size_t Literal(uint64_t value) { cursor = (cursor + 7) & ~size_t(7); const size_t at = cursor; Store(code + at, value); cursor += 8; return at; }
 uint32_t LoadLiteral(size_t at, size_t literal, unsigned reg) {
  return 0x58000000 | ((uint32_t((int64_t(literal) - int64_t(at)) / 4) & 0x7ffff) << 5) | reg;
 }
 size_t Thunk(void *target) {
  cursor = (cursor + 7) & ~size_t(7); const size_t at = cursor; cursor += 16;
  Emit(at, 0x58000050); Emit(at + 4, 0xd61f0200); Store(code + at + 8, uint64_t(target)); return at;
 }
 explicit Fixture(bool nativeHelpers = false, bool loadNative = true) : image(loadNative), useNativeHelpers(nativeHelpers) {
  if (loadNative) {
  code = static_cast<uint8_t *>(mmap(nullptr, 0x10000, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0));
  Require(code != MAP_FAILED, "allocate local native fixture"); pthread_jit_write_protect_np(0);
  Require(image.Word(kStart) == 0xd103c3ff, "pristine native entry (patched/app entry cannot run in this fixture)");
  for (uint64_t pc = kStart; pc < kEnd; pc += 4) Emit(size_t(pc - kStart), image.Word(pc));
  // Verify the sampled consumer and equality/back-edge seam before executing.
  Require(image.Word(0x10005729c) == 0xf9401397 && image.Word(0x100057224) == 0x1e232020 &&
          image.Word(0x100057228) == 0x1e200440 && image.Word(0x100057248) == 0x54fffea2, "verified native loop seam");
  for (uint64_t pc = kStart; pc < kEnd; pc += 4) {
   const uint32_t word = image.Word(pc); const size_t offset = size_t(pc - kStart);
   if ((word & 0x9f000000) == 0x90000000) {
    const uint32_t immediate = ((word >> 29) & 3) | (((word >> 5) & 0x7ffff) << 2);
    const uint64_t page = uint64_t(int64_t(pc & ~uint64_t(4095)) + Signed(immediate, 21) * 4096);
    uint8_t *target = scanPage.data();
    if (page == 0x10058e000) target = reinterpret_cast<uint8_t *>(sectors.data()) - 0x358;
    else if (page == 0x10060b000) target = scanPage.data();
    else if (page == 0x1006b2000) target = modelPage.data();
    Emit(offset, LoadLiteral(offset, Literal(uint64_t(target)), word & 31));
   } else if ((word & 0xfc000000) == 0x94000000) {
    const uint64_t destination = uint64_t(int64_t(pc) + Signed(word & 0x3ffffff, 26) * 4);
    if (destination >= kStart && destination < kEnd &&
        (nativeHelpers || (destination != 0x100058154 && destination != 0x100058a3c))) continue;
    void *target = reinterpret_cast<void *>(Unused);
    if (destination == 0x10012c0c4) target = reinterpret_cast<void *>(Clear);
    else if (destination == 0x10014a6b4) target = reinterpret_cast<void *>(Assert);
    else if (destination == 0x100058154 || destination == 0x100058a3c) target = reinterpret_cast<void *>(OtherCar);
    else if (destination == 0x100152fa8) target = reinterpret_cast<void *>(Centre);
    else if (destination == 0x10010c0f4 || destination == 0x10010c0b4) target = reinterpret_cast<void *>(Player);
    else if (destination == 0x1002f50e8) target = reinterpret_cast<void *>(Atan);
    else if (destination == 0x1002f4ef0) target = reinterpret_cast<void *>(SinCos);
    Emit(offset, Branch(offset, Thunk(target), 0x94000000));
   }
  }
  // Only a completed sector pass is counted. Finite convergent native behavior
  // is untouched; a repeated unordered comparison returns at a bounded seam.
  cursor = (cursor + 7) & ~size_t(7); const size_t guard = cursor; cursor += 36;
  Emit(guard, LoadLiteral(guard, Literal(uint64_t(&passes)), 8));
  Emit(guard + 4, 0xb9400109); // ldr w9,[x8]
  Emit(guard + 8, 0x11000529); // add w9,w9,#1
  Emit(guard + 12, 0xb9000109); // str w9,[x8]
  Emit(guard + 16, 0x7101013f); // cmp w9,#64
  Emit(guard + 20, 0x54000003 | ((uint32_t((int64_t(0x1ec) - int64_t(guard + 20)) / 4) & 0x7ffff) << 5)); // b.lo native compare
  Emit(guard + 24, 0x52afff08); // mov w8,#0x7ff80000 (nonfinite marker)
  Emit(guard + 28, 0x1e270100); // fmov s0,w8
  Emit(guard + 32, Branch(guard + 32, 0xd80)); // native epilogue
  const size_t back = 0x100057248 - kStart;
  Emit(back, 0x54000002 | ((uint32_t((int64_t(guard) - int64_t(back)) / 4) & 0x7ffff) << 5));
  sys_icache_invalidate(code, 0x10000); pthread_jit_write_protect_np(1);
  run = reinterpret_cast<Weave>(code);
  }
  Store(modelPage.data() + 0x50, model); Store(model + 0x20, collision); Store(collision + 0x1c, 1.0f);
  Reset();
 }
 ~Fixture() { if (code) munmap(code, 0x10000); }
 void *CarHelper() { return useNativeHelpers ? code + (0x100058154 - kStart) : reinterpret_cast<void *>(OtherCar); }
 void *ObjectHelper() { return useNativeHelpers ? code + (0x100058a3c - kStart) : reinterpret_cast<void *>(OtherCar); }
 void *PedHelper() { return code + (0x1000586e0 - kStart); }
 void Reset() {
  active = this; passes = helperCalls = 0; poisonHelper = oscillatingHelper = false;
  std::memset(sectors.data(), 0, sizeof sectors); std::memset(vehicle, 0, sizeof vehicle); std::memset(obstacle, 0, sizeof obstacle);
  Store(scanPage.data() + 0x358, uint16_t(1)); Store(vehicle + 0x38, -400.0f);
  Store(obstacle + 0x38, -400.0f); Store(obstacle + 0x3c, 1.0f); Store(obstacle + 0x60, uint32_t(0x100));
  Store(vehicle + 8, 1.0f); Store(vehicle + 0x1c, 1.0f); Store(vehicle + 0x30, 1.0f);
  Store(obstacle + 8, 1.0f); Store(obstacle + 0x1c, 1.0f);
 }
 void Car(Node &node) { node = {obstacle}; sectors[40 * 80 + 40].lists[4] = &node; }
};
} // namespace nativeweavetest
