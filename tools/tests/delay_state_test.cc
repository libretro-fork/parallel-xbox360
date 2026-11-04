/* Real PPC syscall import, generated export trampoline, and fresh libco stacks. */
#include "xenia/emulator.h"
#include "xenia/base/byte_stream.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include "xenia/cpu/raw_module.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/export_resolver.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/guest_scheduler.h"
#include "xenia/kernel/xthread.h"
#include <cstdio>
#include <cstdlib>
using xe::X_STATUS;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %u\n", unsigned(__LINE__)); std::abort(); } } while (0)
static uint32_t nested_code;
static void nested_delay(xe::cpu::ppc::PPCContext* context,
                         xe::kernel::KernelState* kernel) {
  uint64_t args[] = {context->r[3]};
  kernel->processor()->Execute(xe::kernel::XThread::GetCurrentThread()->thread_state(),
                               nested_code, args, 1);
}

int main() {
  xe::InitializeLogging("delay_state_test");
  xe::Profiler::Initialize();
  xe::Emulator emulator({}, "build/tests/state-runtime", "build/tests/state-runtime", "build/tests/state-runtime");
  CHECK(emulator.Setup(true, {}, {}, {}) == X_STATUS_SUCCESS);
  // This fixture allocates code outside the title's static code range.
  emulator.processor()->EnableDynamicCode();
  auto* memory = emulator.memory();
  auto* kernel = emulator.kernel_state();
  auto* scheduler = kernel->guest_scheduler();
  uint32_t code = memory->SystemHeapAlloc(4096);
  uint32_t data = memory->SystemHeapAlloc(4096);
  CHECK(code && data);
  const uint32_t instructions[] = {0x7FE802A6, 0x7C7E1B78, 0x38600000,
      0x38800000, 0x38BE0008, 0x4800006D, 0x38800001, 0x909E0000,
      0x7FE803A6, 0x4E800020};
  for (unsigned i = 0; i < sizeof(instructions)/sizeof(instructions[0]); ++i)
    xe::store_and_swap<uint32_t>(memory->TranslateVirtual(code + i * 4), instructions[i]);
  xe::store_and_swap<uint32_t>(memory->TranslateVirtual(code + 128), 0x44000042);
  xe::store_and_swap<uint32_t>(memory->TranslateVirtual(code + 132), 0x4E800020);
  xe::store_and_swap<uint32_t>(memory->TranslateVirtual(code + 160), 0x44000042);
  xe::store_and_swap<uint32_t>(memory->TranslateVirtual(code + 164), 0x4E800020);
  auto module = std::make_unique<xe::cpu::RawModule>(emulator.processor());
  module->SetAddressRange(code, 168);
  xe::cpu::Function* function;
  module->DeclareFunction(code + 128, &function);
  auto* entry = emulator.export_resolver()->GetExportByOrdinal("xboxkrnl.exe", 0x5A);
  CHECK(entry && entry->function_data.trampoline);
  static_cast<xe::cpu::GuestFunction*>(function)->SetupExtern(
      reinterpret_cast<xe::cpu::GuestFunction::ExternHandler>(entry->function_data.trampoline), entry);
  function->set_status(xe::cpu::Symbol::Status::kDeclared);
  module->DeclareFunction(code + 160, &function);
  static_cast<xe::cpu::GuestFunction*>(function)->SetupExtern(nested_delay);
  function->set_status(xe::cpu::Symbol::Status::kDeclared);
  nested_code = code;
  auto* owned_module = module.get();
  CHECK(emulator.processor()->AddModule(std::move(module)));
  const size_t symbols_before = owned_module->QuerySymbolCount();
  CHECK(emulator.processor()->LookupImportQuiescent(code + 128) != nullptr);
  CHECK(!emulator.processor()->LookupImportQuiescent(code + 64));
  CHECK(owned_module->QuerySymbolCount() == symbols_before);
  xe::kernel::object_ref<xe::kernel::XThread> threads[6];
  uint32_t handles[6];
  scheduler->SetPaused(true);
  CHECK(scheduler->WaitUntilPaused(2000000));
  xe::store_and_swap<uint64_t>(memory->TranslateVirtual(data + 128 + 8), uint64_t(-2000000LL));
  {
    auto nested = xe::kernel::object_ref<xe::kernel::XThread>(new xe::kernel::XThread(
        kernel, 128 * 1024, 0, code + 160, data + 128, 0, true, false, kernel->GetSystemProcess()));
    CHECK(nested->Create() == X_STATUS_SUCCESS);
    bool captured = false;
    for (unsigned attempt = 0; attempt < 20 && !captured; ++attempt) {
      scheduler->SetPaused(false);
      const uint64_t started = xe::Clock::QueryHostUptimeMillis();
      while (xe::Clock::QueryHostUptimeMillis() - started < 25) {}
      scheduler->SetPaused(true);
      CHECK(scheduler->WaitUntilPaused(2000000));
      captured = nested->thread_state()->context()->checkpoint_export_pc == UINT32_MAX;
    }
    CHECK(captured);
    xe::ByteStream rejected(nullptr, SIZE_MAX, 0, true);
    CHECK(!nested->Save(&rejected));
    scheduler->SetPaused(false);
    CHECK(xe::threading::Wait(nested->wait_handle(), false, std::chrono::seconds(3)) == xe::threading::WaitResult::kSuccess);
    CHECK(nested->thread_state()->context()->checkpoint_export_pc == 0);
    nested->ReleaseHandle();
  }
  scheduler->SetPaused(true);
  CHECK(scheduler->WaitUntilPaused(2000000));
  for (unsigned i = 0; i < 6; ++i) {
    xe::store_and_swap<uint64_t>(memory->TranslateVirtual(data + i * 16 + 8), uint64_t(-20000000LL));
    threads[i] = xe::kernel::object_ref<xe::kernel::XThread>(new xe::kernel::XThread(
        kernel, 128 * 1024, 0, code, data + i * 16, 0, true, false, kernel->GetSystemProcess()));
    CHECK(threads[i]->Create() == X_STATUS_SUCCESS);
    threads[i]->SetActiveCpu(static_cast<uint8_t>(i));
    handles[i] = threads[i]->handle();
  }
  CHECK(scheduler->WaitUntilPaused(2000000));
  scheduler->SetPaused(false);
  const uint64_t start = xe::Clock::QueryHostUptimeMillis();
  while (xe::Clock::QueryHostUptimeMillis() - start < 1250) {}
  scheduler->SetPaused(true);
  CHECK(scheduler->WaitUntilPaused(2000000));
  for (auto& thread : threads) {
    CHECK(thread->scheduler_links().wait_kind == static_cast<uint8_t>(xe::kernel::XThread::CooperativeWaitKind::kDelay));
    auto* context = thread->thread_state()->context();
    CHECK(context->checkpoint_export_pc == code + 128);
  }
  for (unsigned round = 0; round < 2; ++round) {
    xe::ByteStream measure(nullptr, SIZE_MAX, 0, true);
    CHECK(emulator.processor()->Save(&measure) && kernel->Save(&measure) && memory->Save(&measure));
    auto* bytes = static_cast<unsigned char*>(std::malloc(measure.offset()));
    CHECK(bytes);
    xe::ByteStream save(bytes, measure.offset());
    CHECK(emulator.processor()->Save(&save) && kernel->Save(&save) && memory->Save(&save));
    for (auto& thread : threads) {
      CHECK(thread->Terminate(0) == X_STATUS_SUCCESS);
      thread->ReleaseHandle();
      thread.reset();
    }
    xe::ByteStream load(bytes, save.offset());
    CHECK(emulator.processor()->Restore(&load) && kernel->Restore(&load) && memory->Restore(&load));
    CHECK(!load.remaining());
    std::free(bytes);
    for (unsigned i = 0; i < 6; ++i) {
      threads[i] = kernel->object_table()->LookupObject<xe::kernel::XThread>(handles[i]);
      CHECK(threads[i]);
      threads[i]->ActivateRestored();
    }
  }
  const uint64_t resumed = xe::Clock::QueryHostUptimeMillis();
  scheduler->SetPaused(false);
  for (auto& thread : threads) {
    CHECK(xe::threading::Wait(thread->wait_handle(), false, std::chrono::seconds(3)) == xe::threading::WaitResult::kSuccess);
    CHECK(thread->thread_state()->context()->r[4] == 1);
    thread->ReleaseHandle();
    thread.reset();
  }
  const uint64_t remaining_elapsed = xe::Clock::QueryHostUptimeMillis() - resumed;
  CHECK(remaining_elapsed >= 100 && remaining_elapsed < 1300);
  for (unsigned i = 0; i < 6; ++i)
    CHECK(xe::load_and_swap<uint32_t>(memory->TranslateVirtual(data + i * 16)) == 1);
  emulator.Shutdown();
  std::puts("PASS: six real KeDelayExecutionThread imports survive two restores before scheduling, resume after remaining delay, and reject nested HLE frames");
}
