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
    auto pending = std::unique_ptr<spv::Block>(spv::Block::create(builder.getUniqueId(), *second));
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
  {
    spv::Builder builder(spv::Spv_1_3, 0, nullptr);
    auto* function = builder.makeEntryPoint("instruction-chain");
    auto* block = function->getEntryBlock();
    auto* label = block->getFirstInstruction();
    CHECK(label->getBlock() == block && !block->getNextInstruction(label));
    CHECK(!block->isTerminated() && !block->getMergeInstruction());
    auto first = builder.makeInstruction(spv::Op::OpNop);
    auto* firstRaw = first.get();
    block->addInstruction(std::move(first));
    CHECK(block->getNextInstruction(label) == firstRaw && !firstRaw->getBlock());
    auto merge = builder.makeInstruction(spv::Op::OpSelectionMerge);
    auto* mergeRaw = merge.get();
    block->addInstruction(std::move(merge));
    block->addInstruction(builder.makeInstruction(spv::Op::OpReturn));
    CHECK(block->getMergeInstruction() == mergeRaw && block->isTerminated());
    auto before = builder.makeInstruction(spv::Op::OpNop);
    auto* beforeRaw = before.get();
    block->insertInstruction(firstRaw, mergeRaw, std::move(before));
    CHECK(firstRaw->getNextInstruction() == beforeRaw && block->getMergeInstruction() == mergeRaw);
    auto beforeTail = builder.makeInstruction(spv::Op::OpNop);
    block->insertInstruction(mergeRaw, mergeRaw->getNextInstruction(), std::move(beforeTail));
    CHECK(!block->getMergeInstruction() && block->isTerminated());
    block->rewriteAsCanonicalUnreachableMerge();
    CHECK(block->getFirstInstruction() == label && block->getNextInstruction(label)->getOpCode() == spv::Op::OpUnreachable);
    CHECK(!block->getMergeInstruction() && block->isTerminated());
    block->rewriteAsCanonicalUnreachableContinue(block);
    CHECK(block->getNextInstruction(label)->getOpCode() == spv::Op::OpBranch);
    CHECK(!block->getNextInstruction(block->getNextInstruction(label)));
    auto* local = builder.createInstruction(spv::Op::OpVariable);
    block->addLocalVariable(std::unique_ptr<spv::Instruction>(local));
    CHECK(block->getFirstLocalVariable() == local && !local->getNextInstruction());
    CHECK(!local->getBlock());
  }
  for (unsigned unwind = 0; unwind < 2; ++unwind) {
    const size_t initial = xe_spv_test_live();
    try {
      spv::Builder builder(spv::Spv_1_3, 0, nullptr);
      xe_spv_instruction_chain first = {}, second = {};
      spv::Instruction* heads[2] = {};
      for (unsigned i = 0; i < 2048; ++i) {
        auto record = builder.makeInstruction(spv::Op::OpNop);
        // Heap operands prove typed cleanup, not just arena-page release.
        record->reserveOperands(16);
        record->addImmediateOperand(i);
        if (i < 2) heads[i] = record.get();
        builder.appendInstruction(i & 1 ? second : first, std::move(record));
        if (unwind && i == 1023) throw std::bad_alloc();
      }
      CHECK(builder.firstInstruction(first) == heads[0] && builder.firstInstruction(second) == heads[1]);
      unsigned counts[2] = {};
      for (unsigned owner = 0; owner < 2; ++owner) {
        for (auto* i = builder.firstInstruction(owner ? second : first); i; i = i->getNextInstruction()) {
          CHECK(i->getImmediateOperand(0) == 2 * counts[owner] + owner);
          CHECK(!i->getBlock());
          ++counts[owner];
        }
        CHECK(counts[owner] == 1024);
      }
    } catch (const std::bad_alloc&) { CHECK(unwind); }
    CHECK(xe_spv_test_live() == initial);
  }
  // Fault the two actual object-page growth paths, not unrelated C++ metadata.
  for (unsigned kind = 0; kind < 2; ++kind) {
    const size_t before = xe_spv_test_pages();
    {
      spv::Builder builder(spv::Spv_1_3, 0, nullptr);
      builder.makeEntryPoint("first");
      auto type = builder.makeVoidType();
      if (kind == 0) {
        for (unsigned i = 1; i < 32; ++i) {
          spv::Block* entry;
          builder.makeFunctionEntry(spv::NoPrecision, type, "f", spv::LinkageType::Max, {}, {}, &entry);
        }
      } else {
        for (unsigned i = 1; i < 64; ++i) builder.makeNewBlock();
      }
      spv::Block* previous = builder.getBuildPoint();
      spv::Block* output = previous;
      bool failed = false;
      xe_spv_test_page_fail_after(xe_spv_test_page_calls());
      try {
        builder.makeFunctionEntry(spv::NoPrecision, type, "failed", spv::LinkageType::Max, {}, {}, &output);
      } catch (const std::bad_alloc&) { failed = true; }
      xe_spv_test_page_fail_after(size_t(-1));
      CHECK(failed && output == previous && builder.getBuildPoint() == previous);
    }
    CHECK(xe_spv_test_pages() == before && !xe_spv_test_live());
  }
  {
    const size_t before = xe_spv_test_pages();
    {
      spv::InstructionArena small(128);
      spv::Builder builder(spv::Spv_1_3, 0, nullptr, small.get());
      auto* function = builder.makeEntryPoint("failed label");
      while (xe_spv_arena_used(small.get()) < 128) builder.createInstruction(spv::Op::OpNop);
      bool failed = false;
      xe_spv_test_page_fail_after(xe_spv_test_page_calls());
      try { spv::Block::create(builder.getUniqueId(), *function); }
      catch (const std::bad_alloc&) { failed = true; }
      xe_spv_test_page_fail_after(size_t(-1));
      CHECK(failed);
      // Object pages must be cleaned before instruction orphans, including
      // fully constructed detached blocks not adopted by the function.
      spv::Block* detached = spv::Block::create(builder.getUniqueId(), *function);
      detached->addInstruction(builder.makeInstruction(spv::Op::OpNop));
    }
    CHECK(xe_spv_test_pages() == before && !xe_spv_test_live());
  }
  {
    spv::Builder builder(spv::Spv_1_3, 0, nullptr);
    spv::Block* entry;
    auto* unnamed = builder.makeFunctionEntry(spv::NoPrecision, builder.makeVoidType(), nullptr,
        spv::LinkageType::Max, {}, {}, &entry);
    CHECK(!unnamed->getExportName()[0]);
    char name[] = "long_borrowed_function_name_for_export(u32,f32)";
    auto* exported = builder.makeFunctionEntry(spv::NoPrecision, builder.makeVoidType(), name,
        spv::LinkageType::Export, {}, {}, &entry);
    std::memset(name, 'x', sizeof(name) - 1);
    CHECK(!std::strcmp(exported->getExportName(), "long_borrowed_function_name_for_export"));
  }
  {
    const size_t before = xe_spv_test_pages();
    {
      spv::InstructionArena workspace(128);
      spv::Module module(workspace.get());
      module.mapInstruction(module.createInstruction(1, spv::NoType, spv::Op::OpTypeVoid));
      auto* type = module.createInstruction(2, spv::NoType, spv::Op::OpTypeFunction);
      type->addIdOperand(1);
      module.mapInstruction(type);
      {
        std::unique_ptr<spv::Function> function(spv::Function::create(3, 1, 2, 0,
            spv::LinkageType::Max, nullptr, module));
        CHECK(module.getInstruction(3));
      }
      CHECK(!module.getInstruction(3));
      // Make the next constructor run out of instruction pages mid-parameter list.
      for (unsigned i = 0; i < 200; ++i) type->addIdOperand(1);
      bool failed = false;
      xe_spv_test_page_fail_after(xe_spv_test_page_calls());
      try { spv::Function::create(3, 1, 2, 4, spv::LinkageType::Max, nullptr, module); }
      catch (const std::bad_alloc&) { failed = true; }
      xe_spv_test_page_fail_after(size_t(-1));
      CHECK(failed && !module.getInstruction(3));
      CHECK(module.getInstruction(4)); // still-live arena-owned partial parameters
    }
    CHECK(xe_spv_test_pages() == before && !xe_spv_test_live());
  }
  std::puts("PASS: borrowed/null function names, owned export spelling and failed/retired inline-ID unpublication");
  std::puts("PASS: pooled function/block growth faults, unpublished state, failed constructor and detached-block cleanup");
  std::puts("PASS: separate borrowed global lists, stable order and arena-owned typed operand unwind");
  std::puts("PASS: typed instruction ownership, label identity, splice and canonical rewrites");
  std::puts("PASS: typed block-chain adoption, duplicate/foreign rejection and creation order");
  std::puts("PASS: 0/1/4/9/70 parameters and partial-parameter construction cleanup");
  std::puts("PASS: 50000 typed ID mappings across module resets without warm index growth");
  std::puts("PASS: 60200 arena records, orphan/exception cleanup, operand fault and pending-owner reset");
  std::puts("PASS: two retained worker arenas, 960000 records without page growth, partial-construction and active-owner unwind");
  std::puts("PASS: larger-than-reserve modules retain their checked high-water pages");
  return 0;
}
