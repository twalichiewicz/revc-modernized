// Runs pristine 5217 query dispatch and LOS sphere math on isolated objects.
// Contact/vertical narrow phases are observers, NOT physical response tests.
// No plugin, game launch, RenderWare publication or solver is involved.
#include "nativejetshadowarm.h"
#include <array>

namespace {
using namespace nativejetshadowtest;
using Bytes = std::array<uint8_t, 0x6f0>;
Bytes aircraft[2]{}, building{};
std::array<uint8_t, 0x68> intact{}, removed{}, wall{};
std::array<uint8_t, 0x40> planeInfo{}, buildingInfo{};
std::array<uint8_t, 20> sphere{};
std::array<uint8_t, 16> rwObjects[2]{};
struct Vector { float x, y, z; };
struct Line { Vector a; uint32_t padA; Vector b; uint32_t padB; };
struct Node { uint8_t *entity; Node *previous; Node *next; };
static_assert(sizeof(Line) == 32 && sizeof(Node) == 24);
using Test = bool (*)(const Line &, const void *, void *, bool, bool);
using Clear = bool (*)(Node **, const Line &, bool, bool);
using Vertical = bool (*)(Node **, const Line &, void *, float &, uint8_t *&, bool, void *);
using Pair = int (*)(uint8_t *, uint8_t *, void *);
Test test;
void *lastA{}, *lastB{}, *lastVertical{}, *lastTest{};
unsigned pairCalls{}, verticalCalls{}, testCalls{}, facts{}, failures{}, authorityFailures{};

void Check(bool condition, const char *name) {
    ++facts;
    if (!condition) { ++failures; std::printf("FACT FAIL: %s\n", name); }
}
void Authority(bool condition, const char *name) {
    if (!condition) { ++authorityFailures; std::printf("AUTHORITY MISSING: %s\n", name); }
}
int ObservePair(const void *, void *a, const void *, void *b, void *, void *, float *) {
    ++pairCalls; lastA = a; lastB = b;
    return 0; // Avoid native response/bookkeeping; test actual argument routing.
}
bool ObserveVertical(const Line &, const void *, void *col, void *, float &, bool, bool, void *) {
    ++verticalCalls; lastVertical = col;
    return false; // This observer does not claim a native vertical hit/miss.
}
bool ObserveTest(const Line &line, const void *matrix, void *col, bool see, bool shoot) {
    ++testCalls; lastTest = col;
    return test(line, matrix, col, see, shoot); // Real native narrow-phase math.
}
void SphereOnlyPlanes(uint8_t *col) {
    Require(Read<int16_t>(col + 0x2c) == 0, "sphere-only fixture must not request triangle planes");
}
void ResetScan(uint8_t *entity) { Store(entity + 0x66, uint16_t(0)); }
void Entity(uint8_t *e, int model, unsigned kind) {
    Store(e + 8, Vector{1, 0, 0}); Store(e + 0x18, Vector{0, 1, 0});
    Store(e + 0x28, Vector{0, 0, 1}); Store(e + 0x38, Vector{0, 0, 0});
    Store(e + 0x60, uint32_t(0x100 | kind)); Store(e + 0x6a, int16_t(model));
    if (kind == 2) Store(e + 0x348, int32_t(4)); // CPlane, not an automobile.
}
void Bounds(uint8_t *col) {
    Store(col, Vector{0, 0, 0}); Store(col + 12, 2.f);
    Store(col + 0x10, Vector{-2, -2, -2}); Store(col + 0x1c, Vector{2, 2, 2});
}
}

int main(int argc, char **argv) {
    const bool requireAuthority = argc == 2 && !std::strcmp(argv[1], "--require-authority");
    Require(argc == 1 || requireAuthority, "only --require-authority is supported");
    Code native;
    const auto pairAt = native.Add(0x100155af0, 0x100155d50);
    const auto clearAt = native.Add(0x10012f5c8, 0x10012f71c);
    const auto verticalAt = native.Add(0x10012e38c, 0x10012e488);
    const auto testAt = native.Add(0x100049f7c, 0x10004a440);
    native.Add(0x100049530, 0x1000497e8); // TestLineBox
    native.Add(0x1001723fc, 0x1001724d0); // Invert(CMatrix)
    native.Add(0x100172f48, 0x100172f98); // Matrix * Vector
    native.bindings[0x10004c40c] = reinterpret_cast<void *>(ObservePair);
    // Internal copied targets take precedence over bindings. Observe the LOS
    // consumer by redirecting only its external call site after relocation.
    native.bindings[0x10004c028] = reinterpret_cast<void *>(ObserveVertical);
    native.bindings[0x10004a440] = reinterpret_cast<void *>(SphereOnlyPlanes);
    // Provide a contiguous isolated model table rather than relying on the
    // harness's individually relocated 4KiB pages for model1868's entry.
    native.pages[0x1006b2000] = std::make_unique<uint8_t[]>(0x50 + 6500 * sizeof(void *));
    native.Finish();
    // Make LOS-clear's two direct calls go through the observer, which then
    // executes the complete copied TestLineOfSight routine unchanged.
    pthread_jit_write_protect_np(0);
    for (uint64_t pc : {0x10012f624ull, 0x10012f6dcull}) {
        const size_t at = size_t(static_cast<uint8_t *>(native.Address(pc)) - native.code);
        const size_t thunk = native.Thunk(reinterpret_cast<void *>(ObserveTest));
        Store(native.code + at, uint32_t(0x94000000 | (uint32_t((int64_t(thunk)-int64_t(at))/4) & 0x3ffffff)));
    }
    sys_icache_invalidate(native.code, 0x10000); pthread_jit_write_protect_np(1);
    auto pair = reinterpret_cast<Pair>(native.code + pairAt);
    auto clear = reinterpret_cast<Clear>(native.code + clearAt);
    auto vertical = reinterpret_cast<Vertical>(native.code + verticalAt);
    test = reinterpret_cast<Test>(native.code + testAt);
    // Static inverse matrix's guard is initialized; no CRT constructors run.
    Store(native.Page(0x1007b7c00), uint8_t(1));
    Store(native.Page(0x10060b358), uint16_t(17));
    Store(native.Page(0x10060b508), static_cast<void *>(nullptr));
    auto table = reinterpret_cast<void **>(native.Page(0x1006b2050));
    table[181] = planeInfo.data();
    constexpr int buildingModel = 1868; // doontoon22; geometry here is synthetic.
    table[buildingModel] = buildingInfo.data();
    Store(planeInfo.data() + 0x20, intact.data()); Store(buildingInfo.data() + 0x20, wall.data());
    for (int i = 0; i < 2; ++i) {
        Entity(aircraft[i].data(), 181, 2);
        Store(aircraft[i].data() + 0x58, rwObjects[i].data());
    }
    Entity(building.data(), buildingModel, 1);
    Bounds(intact.data()); Bounds(removed.data()); Bounds(wall.data());
    Store(intact.data() + 0x28, int16_t(1)); Store(intact.data() + 0x38, sphere.data());
    Store(sphere.data(), Vector{0, 0, 0}); Store(sphere.data() + 12, 1.f);
    const Line line{{-3, 0, 0}, 0, {3, 0, 0}, 0};
    std::array<uint8_t, 64 * 40> points{};
    Check(test(line, aircraft[0].data() + 8, intact.data(), false, false), "real native LOS hits intact sphere");
    Check(!test(line, aircraft[0].data() + 8, removed.data(), false, false), "real native LOS misses replacement with sphere removed");
    for (int i = 0; i < 2; ++i) {
        auto e = aircraft[i].data();
        Check(pair(e, building.data(), points.data()) == 0 && lastA == intact.data() && lastB == wall.data(), "physical caller selects shared model COL and building COL");
        Authority(lastA == (i == 0 ? removed.data() : intact.data()), "physical contact has no per-instance geometry replacement");
        Node node{e, nullptr, nullptr}; auto head = &node;
        ResetScan(e); const unsigned before = testCalls;
        Check(!clear(&head, line, false, false) && testCalls == before + 1 && lastTest == intact.data(), "LOS-clear dispatches shared COL and native math blocks ray");
        Authority(lastTest == (i == 0 ? removed.data() : intact.data()), "LOS-clear has no per-instance geometry replacement");
        float fraction = 1; uint8_t *hit = nullptr; ResetScan(e); const auto beforeVertical = verticalCalls;
        Check(!vertical(&head, line, points.data(), fraction, hit, false, nullptr) && verticalCalls == beforeVertical + 1 && lastVertical == intact.data(), "vertical caller selects shared model COL");
        Authority(lastVertical == (i == 0 ? removed.data() : intact.data()), "vertical query has no per-instance geometry replacement");
    }
    // Removing the entire RW object is a deliberately extreme read-only
    // control. This is NOT a rendered tear or RenderWare writeback test.
    Store(aircraft[0].data() + 0x58, static_cast<void *>(nullptr));
    Node node{aircraft[0].data(), nullptr, nullptr}; auto head = &node;
    ResetScan(node.entity);
    Check(!clear(&head, line, false, false) && lastTest == intact.data(), "no RW object still leaves original native COL occlusion");
    // Replacing the shared model pointer changes both instances: it cannot
    // implement an instance-local torn wing without query routing.
    Store(planeInfo.data() + 0x20, removed.data());
    for (auto &e : aircraft) {
        node.entity = e.data(); ResetScan(node.entity);
        Check(clear(&head, line, false, false) && lastTest == removed.data(), "global model COL replacement affects both aircraft");
    }
    Check(pairCalls == 2 && verticalCalls == 2, "all observed native callers executed");
    std::printf("facts=%u failures=%u authority_missing=%u scope=offline-native-dispatch-and-LOS-sphere\n", facts, failures, authorityFailures);
    if (failures) return 2;
    return requireAuthority && authorityFailures ? 1 : 0;
}
