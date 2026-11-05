/* Typed lifetime interop over the production C89 allocator. */
#include "xenia/gpu/spirv/SpvBuilder.h"
#include "core_spv_arena_test_alloc.h"
#include <cstdio>
#include <cstring>
#include <type_traits>
static_assert(sizeof(spv::Instruction) == 64);
static_assert(std::is_standard_layout_v<spv::Instruction>);
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(int argc, char**) {
  xe_spv_arena arena = {};
  CHECK(xe_spv_arena_reserve(&arena, 512));
  size_t allocations = (arena.reserved_records / XE_SPV_RECORDS_PER_PAGE);
  for (unsigned round = 0; round < 200; ++round) {
    try {
      spv::Builder builder(spv::Spv_1_3, 0, nullptr, &arena);
      auto pending = builder.makeInstruction(spv::Op::OpNop);
      pending->reserveOperands(100);
      CHECK(xe_spv_test_live() == 1);
      for (unsigned i = 0; i < 300; ++i) {
        auto orphan = builder.createInstruction(spv::Op::OpNop);
        CHECK(!(uintptr_t(orphan) & 63));
        orphan->addImmediateOperand(i);
        orphan->reserveOperands(100);
      }
      CHECK(xe_spv_test_live() == 301);
      spv::Instruction* retired = pending.get();
      pending.reset();
      if (argc > 1) return int(retired->getNumOperands());
      CHECK(xe_spv_test_live() == 300);
      bool rejected = false;
      try { spv::Builder overlap(spv::Spv_1_3, 0, nullptr, &arena); }
      catch (const std::bad_alloc&) { rejected = true; }
      CHECK(rejected && arena.in_use && xe_spv_test_live() == 300);
      if (round & 1) throw std::bad_alloc();
    } catch (const std::bad_alloc&) {}
    CHECK(!arena.in_use && xe_spv_test_live() == 0);
    CHECK((arena.reserved_records / XE_SPV_RECORDS_PER_PAGE) == allocations);
  }
  {
    spv::Builder builder(spv::Spv_1_3, 0, nullptr, &arena);
    spv::Id type = builder.makeUintType(32);
    std::vector<spv::Id> params(9, type);
    spv::Block* entry;
    auto function = builder.makeFunctionEntry(spv::NoPrecision, type, "parameters",
        spv::LinkageType::Max, params, {}, &entry);
    for (unsigned i = 0; i < 9; ++i) CHECK(function->getParamType(i) == type);
    builder.makeReturn(true, function->getParamId(0));
    builder.leaveFunction();
  }
  CHECK(!arena.in_use && xe_spv_test_live() == 0);
  {
    spv::Builder builder(spv::Spv_1_3, 0, nullptr, &arena);
    auto orphan = builder.createInstruction(spv::Op::OpNop);
    orphan->reserveOperands(16);
    orphan->addImmediateOperand(77);
    xe_spv_test_fail_next();
    bool failed = false;
    try { orphan->reserveOperands(256); }
    catch (const std::bad_alloc&) { failed = true; }
    CHECK(failed && orphan->getImmediateOperand(0) == 77);
  }
  CHECK(!arena.in_use && xe_spv_test_live() == 0);
  // The translator Reset contract: pending detached owners precede builder reset.
  {
    auto builder = std::make_unique<spv::Builder>(spv::Spv_1_3, 0, nullptr, &arena);
    auto pending = builder->makeInstruction(spv::Op::OpSwitch);
    pending->reserveOperands(128);
    pending.reset();
    builder.reset();
  }
  CHECK(!arena.in_use && xe_spv_test_live() == 0);
  CHECK(xe_spv_arena_close(&arena));
  std::puts("PASS: 60200 arena records, orphan/exception cleanup, operand fault and pending-owner reset");
  return 0;
}
