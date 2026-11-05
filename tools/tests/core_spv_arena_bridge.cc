/* Typed lifetime interop over the production C89 allocator. */
#include "core_spv_arena_test_alloc.h"
#include "xenia/gpu/spirv/SpvBuilder.h"
#include "xenia/gpu/spirv/InstructionArena.h"
#include <cstdio>
#include <cstring>
#include <type_traits>
static_assert(sizeof(spv::Instruction) == 64);
static_assert(std::is_standard_layout_v<spv::Instruction>);
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
// Match the production translator's declaration and reverse destruction order.
struct Worker {
  spv::InstructionArena arena;
  std::unique_ptr<spv::Builder> builder;
  std::unique_ptr<spv::Instruction> pending;
  std::unique_ptr<spv::Instruction> pending_phi;
  void reset() { pending.reset(); pending_phi.reset(); builder.reset(); }
  void start() {
    reset();
    builder = std::make_unique<spv::Builder>(spv::Spv_1_3, 0, nullptr, arena.get());
    pending = builder->makeInstruction(spv::Op::OpSwitch);
    pending->reserveOperands(128);
    pending_phi = builder->makeInstruction(7, 8, spv::Op::OpPhi);
    for (unsigned i = 0; i < 200; ++i) {
      pending_phi->addIdOperand(i + 1);
      pending_phi->addIdOperand(i + 301);
    }
    for (unsigned i = 0; i < 200; ++i) {
      if (!pending_phi->isIdOperand(2 * i) ||
          !pending_phi->isIdOperand(2 * i + 1) ||
          pending_phi->getIdOperand(2 * i) != i + 1 ||
          pending_phi->getIdOperand(2 * i + 1) != i + 301)
        std::abort();
    }
  }
};
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
  for (unsigned count : {0u, 1u, 4u, 9u, 70u}) {
    spv::Builder builder(spv::Spv_1_3, 0, nullptr, &arena);
    spv::Id type = builder.makeUintType(32);
    std::vector<spv::Id> params(count, type);
    spv::Block* entry;
    auto function = builder.makeFunctionEntry(spv::NoPrecision, type, "parameters",
        spv::LinkageType::Max, params, {}, &entry);
    for (unsigned i = 0; i < count; ++i) {
      CHECK(function->getParamType(i) == type);
      CHECK(function->getParamId(i) == function->getParamId(0) + i);
    }
    builder.makeReturn(true, count ? function->getParamId(0) : builder.makeUintConstant(0));
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
  CHECK(xe_spv_test_pages() == 0);
  // Constructor failure after several pages must release its partial reserve.
  xe_spv_test_page_fail_after(xe_spv_test_page_calls() + 3);
  bool failed = false;
  try { Worker worker; }
  catch (const std::bad_alloc&) { failed = true; }
  CHECK(failed && xe_spv_test_pages() == 0);
  xe_spv_test_page_fail_after(size_t(-1));
  for (unsigned unwind = 0; unwind < 2; ++unwind) {
    try {
      Worker first, second;
      size_t calls = xe_spv_test_page_calls();
      for (unsigned i = 0; i < 200; ++i) {
        first.start(); second.start();
        CHECK(first.arena.get()->head != second.arena.get()->head);
        for (unsigned j = 0; j < 1200; ++j) {
          first.builder->createInstruction(spv::Op::OpNop);
          second.builder->createInstruction(spv::Op::OpNop);
        }
        CHECK(xe_spv_test_page_calls() == calls);
      }
      if (unwind) throw std::bad_alloc();
      // Destruction also covers active builders and detached instruction owners.
    } catch (const std::bad_alloc&) {}
    CHECK(xe_spv_test_pages() == 0 && xe_spv_test_live() == 0);
  }
  {
    Worker worker;
    worker.start();
    for (unsigned i = 0; i < 2300; ++i)
      worker.builder->createInstruction(spv::Op::OpNop);
    size_t calls = xe_spv_test_page_calls();
    CHECK(worker.arena.get()->reserved_records > 2048);
    for (unsigned round = 0; round < 100; ++round) {
      worker.start();
      for (unsigned i = 0; i < 2300; ++i)
        worker.builder->createInstruction(spv::Op::OpNop);
      CHECK(xe_spv_test_page_calls() == calls);
    }
  }
  CHECK(xe_spv_test_pages() == 0 && xe_spv_test_live() == 0);
  {
    spv::InstructionArena workspace(256);
    size_t warmed = 0;
    for (unsigned round = 0; round < 100; ++round) {
      {
        spv::Module module(workspace.get());
        CHECK(!module.getInstruction(1498));
        for (unsigned i = 0; i < 500; ++i) {
          auto* instruction = module.createInstruction(i * 3 + 1, spv::NoType, spv::Op::OpNop);
          module.mapInstruction(instruction);
          CHECK(module.getInstruction(i * 3 + 1) == instruction);
          CHECK(!module.getInstruction(i * 3 + 2));
        }
        if (!round) warmed = xe_spv_test_index_calls();
        CHECK(xe_spv_test_index_calls() == warmed);
      }
      CHECK(!workspace.get()->index.count);
    }
  }
  CHECK(xe_spv_test_pages() == 0);
  {
    xe_spv_test_index_fail_next();
    bool failed_reserve = false;
    try { spv::InstructionArena rejected(512); }
    catch (const std::bad_alloc&) { failed_reserve = true; }
    CHECK(failed_reserve && xe_spv_test_pages() == 0);
  }
  {
    const size_t before = xe_spv_test_pages();
    bool failed = false;
    try {
      spv::InstructionArena small(128);
      spv::Builder builder(spv::Spv_1_3, 0, nullptr, small.get());
      spv::Id type = builder.makeUintType(32);
      std::vector<spv::Id> parameters(200, type);
      spv::Block* entry = nullptr;
      xe_spv_test_page_fail_after(xe_spv_test_page_calls());
      builder.makeFunctionEntry(spv::NoPrecision, type, "partial parameters",
          spv::LinkageType::Max, parameters, {}, &entry);
    } catch (const std::bad_alloc&) { failed = true; }
    xe_spv_test_page_fail_after(size_t(-1));
    CHECK(failed && xe_spv_test_pages() == before && xe_spv_test_live() == 0);
  }
  {
    spv::Builder builder(spv::Spv_1_3, 0, nullptr);
    auto* first = builder.makeEntryPoint("first");
    auto* state = first->getEntryBlock();
    CHECK(state->updateDebugSourceLocation(-1, 2, 3));
    CHECK(!state->updateDebugSourceLocation(-1, 2, 3));
    CHECK(state->updateDebugScope(17) && !state->updateDebugScope(17));
    state->setUnreachable();
    CHECK(state->updateDebugSourceLocation(4, 2, 3));
    CHECK(state->updateDebugScope(18) && state->isUnreachable());
    spv::Block* secondEntry;
    auto* second = builder.makeFunctionEntry(spv::NoPrecision, builder.makeVoidType(),
        "second", spv::LinkageType::Max, {}, {}, &secondEntry);
    bool duplicate = false, foreign = false;
    try { first->addBlock(first->getEntryBlock()); }
    catch (const std::bad_alloc&) { duplicate = true; }
    auto pending = std::make_unique<spv::Block>(builder.getUniqueId(), *second);
    try { first->addBlock(pending.get()); }
    catch (const std::bad_alloc&) { foreign = true; }
    CHECK(duplicate && foreign && first->getBlockCount() == 1 && second->getBlockCount() == 1);
    CHECK(pending->getTraversalIndex() == UINT32_MAX && !pending->getNextBlock());
    auto* last = pending.get();
    second->addBlock(last);
    pending.release();
    CHECK(secondEntry->getNextBlock() == last && second->getLastBlock() == last);
    CHECK(last->getTraversalIndex() == 1 && !last->getNextBlock());
  }
  std::puts("PASS: typed block-chain adoption, duplicate/foreign rejection and creation order");
  std::puts("PASS: 0/1/4/9/70 parameters and partial-parameter construction cleanup");
  std::puts("PASS: 50000 typed ID mappings across module resets without warm index growth");
  std::puts("PASS: 60200 arena records, orphan/exception cleanup, operand fault and pending-owner reset");
  std::puts("PASS: two retained worker arenas, 960000 records without page growth, partial-construction and active-owner unwind");
  std::puts("PASS: larger-than-reserve modules retain their checked high-water pages");
  return 0;
}
