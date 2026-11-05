// Executes copied pristine 5217 instructions against isolated fixture objects.
// Native CWorld scheduling, CPhysical control/gravity and ApplyMoveSpeed run;
// rendering, AI and collision outcomes are controlled callbacks. No game launch.
#include "nativejetshadowarm.h"
#include <array>
#include <cmath>

namespace {
using namespace nativejetshadowtest;
using Entity = std::array<uint8_t, 0xa00>;
alignas(16) Entity entities[2];
struct Node {
    void *item, *previous, *next;
} nodes[2];
std::array<void *, 32> vtable;
void (*physical)(uint8_t *), (*moveSpeed)(uint8_t *), (*world)();
bool owned[2]{}, suppress = true, stubborn = false;
int control[2]{}, nativeControl[2]{}, collisions[2]{}, shifts[2]{}, moves[2]{}, turns[2]{};

int Index(uint8_t *entity) { return entity == entities[0].data() ? 0 : 1; }
uint32_t &Flags(uint8_t *entity) { return *reinterpret_cast<uint32_t *>(entity + 0x60); }
float &Float(uint8_t *entity, int offset) { return *reinterpret_cast<float *>(entity + offset); }
bool SolverOwned(uint8_t *entity) { return suppress && owned[Index(entity)]; }
void Accept(uint8_t *entity) { Flags(entity) = (Flags(entity) & ~0x2000u) | 0x4000u; }
uintptr_t Quiet(...) { return 0; }
float Power(float a, float b) { return std::pow(a, b); }
void Move(uint8_t *entity) {
    if (SolverOwned(entity))
        return;
    ++moves[Index(entity)];
    moveSpeed(entity);
}
void Turn(uint8_t *entity) {
    if (!SolverOwned(entity))
        ++turns[Index(entity)];
}
void Control(uint8_t *entity) {
    ++control[Index(entity)];
    if (SolverOwned(entity)) {
        Accept(entity);
        return;
    }
    ++nativeControl[Index(entity)];
    physical(entity);
}
void Collision(uint8_t *entity) {
    ++collisions[Index(entity)];
    if (SolverOwned(entity)) {
        Accept(entity);
        return;
    }
    Move(entity);
    if (!stubborn)
        Accept(entity);
}
void Shift(uint8_t *entity) {
    ++shifts[Index(entity)];
    if (SolverOwned(entity)) {
        Accept(entity);
        return;
    }
    Move(entity);
}

void Reset(Code &scheduler, Code &movement, bool stuck) {
    stubborn = stuck;
    std::fill(std::begin(control), std::end(control), 0);
    std::fill(std::begin(nativeControl), std::end(nativeControl), 0);
    std::fill(std::begin(collisions), std::end(collisions), 0);
    std::fill(std::begin(shifts), std::end(shifts), 0);
    std::fill(std::begin(moves), std::end(moves), 0);
    std::fill(std::begin(turns), std::end(turns), 0);
    for (int i = 0; i < 2; ++i) {
        entities[i].fill(0);
        auto e = entities[i].data();
        *reinterpret_cast<void ***>(e) = vtable.data();
        Flags(e) = 0x102; // vehicle, full-physics status, UsesCollision retained
        *reinterpret_cast<uint16_t *>(e + 0x156) = 2; // native gravity enabled
        Float(e, 0x84) = 1;
        Float(e, 0x8c) = .5f;
        Float(e, 0x38) = float(i * 100); // distinguish bodies
        Float(e, 0xd8) = 0;              // native drag coefficient
        nodes[i] = {e, i ? &nodes[0] : nullptr, i == 0 ? &nodes[1] : nullptr};
    }
    Store(scheduler.Page(0x10058e350), &nodes[0]);
    Store(scheduler.Page(0x10058d66c), uint32_t(2));
    Store(scheduler.Page(0x10058d674), 1.f);
    Store(movement.Page(0x10058d674), 1.f);
    Store(scheduler.Page(0x10060b513), uint8_t(0)); // ordinary world path
    Store(scheduler.Page(0x10041d5a8), uint8_t(0)); // no replay override
    Store(scheduler.Page(0x1006268f8), uint8_t(0)); // no native special-gravity cheat
}

bool Scenario(Code &scheduler, Code &movement, bool stuck, bool transfer) {
    Reset(scheduler, movement, stuck);
    owned[0] = true;
    owned[1] = false;
    world();
    const bool ownedClean =
        control[0] == 1 && nativeControl[0] == 0 && collisions[0] == 0 && shifts[0] == 0 &&
        moves[0] == 0 && turns[0] == 0 && Float(entities[0].data(), 0x38) == 0 &&
        Float(entities[0].data(), 0x8c) == .5f && (Flags(entities[0].data()) & 0x100);
    const int expectedMoves = stuck ? 9 : 1;
    const bool nativeIntact =
        nativeControl[1] == 1 && collisions[1] == (stuck ? 6 : 1) && shifts[1] == (stuck ? 2 : 0) &&
        moves[1] == expectedMoves && turns[1] == (stuck ? 1 : 0) &&
        std::fabs(Float(entities[1].data(), 0x8c) - .492f * (stuck ? .707f : 1.f)) < 1e-6f &&
        std::fabs(Float(entities[1].data(), 0x38) - (100.f + (stuck ? 8.707f : 1.f))) < 1e-5f;
    const int firstCollisions = collisions[1], firstShifts = shifts[1], firstMoves = moves[1];
    bool handoff = true;
    if (transfer) {
        owned[0] = false;
        stubborn = false;
        world();
        handoff = nativeControl[0] == 1 && moves[0] == 1 &&
                  std::fabs(Float(entities[0].data(), 0x38) - 1) < 1e-6f;
    }
    std::printf("native world stuck=%d handoff=%d owned_clean=%d native_intact=%d "
                "native_collisions=%d native_shifts=%d native_moves=%d %s\n",
                stuck, transfer, ownedClean, nativeIntact, firstCollisions, firstShifts, firstMoves,
                ownedClean && nativeIntact && handoff ? "PASS" : "FAIL");
    return ownedClean && nativeIntact && handoff;
}
} // namespace

int main(int argc, char **argv) {
    if (argc != 1 && (argc != 2 || std::strcmp(argv[1], "--no-suppression"))) {
        std::fputs("Usage: native-probe [--no-suppression]\n", stderr);
        return 2;
    }
    suppress = !(argc == 2 && !std::strcmp(argv[1], "--no-suppression"));
    Code scheduler, movement;
    scheduler.Add(0x100137f7c, 0x1001388f4); // complete native CWorld::Process
    scheduler.Add(0x100155d50, 0x100155fd4); // native CPhysical::ProcessControl
    scheduler.Add(0x100155fd4, 0x100156050); // ordinary native gravity path
    movement.Add(0x1001563a0, 0x1001563ec);  // complete native ApplyMoveSpeed
    for (const auto &range : scheduler.ranges)
        for (uint64_t pc = range.first; pc < range.last; pc += 4) {
            const auto word = scheduler.image.Word(pc);
            if ((word & 0x7c000000) == 0x14000000) {
                const auto target = uint64_t(int64_t(pc) + Signed(word & 0x3ffffff, 26) * 4);
                if (!scheduler.Address(target))
                    scheduler.bindings[target] = reinterpret_cast<void *>(Quiet);
            }
        }
    scheduler.bindings[0x1001563a0] = reinterpret_cast<void *>(Move);
    scheduler.bindings[0x1001563ec] = reinterpret_cast<void *>(Turn);
    scheduler.bindings[0x1002f570c] = reinterpret_cast<void *>(Power);
    // Branches outside the ordinary fixture lifecycle must fail visibly.
    for (uint64_t pc : {0x1001380a0ull, 0x100138354ull, 0x100138360ull, 0x1001382fcull,
                        0x100156004ull, 0x100138074ull}) {
        const auto word = scheduler.image.Word(pc);
        const auto target = uint64_t(int64_t(pc) + Signed(word & 0x3ffffff, 26) * 4);
        scheduler.bindings[target] = reinterpret_cast<void *>(Code::Unexpected);
    }
    vtable.fill(reinterpret_cast<void *>(Code::Unexpected));
    vtable[0x48 / 8] = reinterpret_cast<void *>(Control);
    vtable[0x50 / 8] = reinterpret_cast<void *>(Collision);
    vtable[0x58 / 8] = reinterpret_cast<void *>(Shift);
    scheduler.Finish();
    pthread_jit_write_protect_np(0);
    movement.Finish();
    world = reinterpret_cast<void (*)()>(scheduler.Address(0x100137f7c));
    physical = reinterpret_cast<void (*)(uint8_t *)>(scheduler.Address(0x100155d50));
    moveSpeed = reinterpret_cast<void (*)(uint8_t *)>(movement.Address(0x1001563a0));
    bool first = Scenario(scheduler, movement, false, false);
    bool second = Scenario(scheduler, movement, true, false);
    bool handoff = Scenario(scheduler, movement, true, true);
    return first && second && handoff ? 0 : 1;
}
