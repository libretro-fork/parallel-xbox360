/* Native ABI fixture: real PPC JIT code, six libco dispatch CPUs and state I/O. */
#include "xenia/emulator.h"
#include "xenia/base/byte_stream.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include "xenia/cpu/raw_module.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/guest_scheduler.h"
#include "xenia/kernel/xthread.h"
#include <cstdio>
#include <cstdlib>
using xe::X_STATUS;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %u\n", unsigned(__LINE__)); std::abort(); } } while (0)
int main() {
  xe::InitializeLogging("scheduler_state_test");
  xe::Profiler::Initialize();
  xe::Emulator emulator({}, "build/tests/state-runtime", "build/tests/state-runtime", "build/tests/state-runtime");
  CHECK(emulator.Setup(true, {}, {}, {}) == X_STATUS_SUCCESS);
  auto* memory = emulator.memory();
  auto* kernel = emulator.kernel_state();
  auto* scheduler = kernel->guest_scheduler();
  uint32_t code = memory->SystemHeapAlloc(4096);
  uint32_t data = memory->SystemHeapAlloc(4096);
  CHECK(code && data);
  const uint32_t instructions[] = {0x80A30004, 0x2C050000, 0x40820010,
      0x38840001, 0x90830000, 0x4BFFFFEC, 0x4E800020};
  for (unsigned i = 0; i < sizeof(instructions)/sizeof(instructions[0]); ++i)
    xe::store_and_swap<uint32_t>(memory->TranslateVirtual(code + i * 4), instructions[i]);
  auto module = std::make_unique<xe::cpu::RawModule>(emulator.processor());
  module->SetAddressRange(code, sizeof(instructions));
  CHECK(emulator.processor()->AddModule(std::move(module)));
  xe::kernel::object_ref<xe::kernel::XThread> threads[6];
  scheduler->SetPaused(true);
  for (unsigned i = 0; i < 6; ++i) {
    threads[i] = xe::kernel::object_ref<xe::kernel::XThread>(new xe::kernel::XThread(
        kernel, 128 * 1024, 0, code, data + i * 16, 0, true, false,
        kernel->GetSystemProcess()));
    CHECK(threads[i]->Create() == X_STATUS_SUCCESS);
    threads[i]->SetActiveCpu(static_cast<uint8_t>(i));
  }
  CHECK(scheduler->WaitUntilPaused(2000000));
  scheduler->SetPaused(false);
  uint64_t deadline = xe::Clock::QueryHostUptimeMillis() + 5000;
  for (;;) {
    bool all = true;
    for (unsigned i = 0; i < 6; ++i)
      all = all && xe::load_and_swap<uint32_t>(memory->TranslateVirtual(data + i * 16)) != 0;
    if (all) break;
    CHECK(xe::Clock::QueryHostUptimeMillis() < deadline);
  }
  scheduler->SetPaused(true);
  CHECK(scheduler->WaitUntilPaused(2000000));
  unsigned char state[16384];
  uint32_t handles[6], saved_counts[6];
  for (unsigned i = 0; i < 6; ++i) {
    auto* context = threads[i]->thread_state()->context();
    context->checkpoint_export_pc = code;
    xe::ByteStream unsupported(nullptr, SIZE_MAX, 0, true);
    CHECK(!threads[i]->Save(&unsupported));
    context->checkpoint_export_pc = 0;
    xe::ByteStream measure(nullptr, SIZE_MAX, 0, true);
    CHECK(threads[i]->Save(&measure));
    CHECK(measure.offset() <= sizeof(state));
    xe::ByteStream output(state, measure.offset());
    CHECK(threads[i]->Save(&output));
    CHECK(output.offset() == measure.offset());
    CHECK(context->last_safepoint_pc >= code && context->last_safepoint_pc < code + sizeof(instructions));
    handles[i] = threads[i]->handle();
    saved_counts[i] = xe::load_and_swap<uint32_t>(memory->TranslateVirtual(data + i * 16));
  }
  xe::ByteStream measure(nullptr, SIZE_MAX, 0, true);
  CHECK(emulator.processor()->Save(&measure));
  CHECK(kernel->Save(&measure));
  CHECK(memory->Save(&measure));
  auto* snapshot = static_cast<unsigned char*>(std::malloc(measure.offset()));
  CHECK(snapshot);
  xe::ByteStream save(snapshot, measure.offset());
  CHECK(emulator.processor()->Save(&save));
  CHECK(kernel->Save(&save));
  CHECK(memory->Save(&save));
  CHECK(save.offset() == measure.offset());
  for (auto& thread : threads) {
    CHECK(thread->Terminate(0) == X_STATUS_SUCCESS);
    thread->ReleaseHandle();
    thread.reset();
  }
  xe::ByteStream load(snapshot, save.offset());
  CHECK(emulator.processor()->Restore(&load));
  CHECK(kernel->Restore(&load));
  CHECK(memory->Restore(&load));
  CHECK(load.remaining() == 0);
  for (unsigned i = 0; i < 6; ++i) {
    CHECK(xe::load_and_swap<uint32_t>(memory->TranslateVirtual(data + i * 16)) == saved_counts[i]);
    threads[i] = kernel->object_table()->LookupObject<xe::kernel::XThread>(handles[i]);
    CHECK(threads[i]);
    threads[i]->ActivateRestored();
  }
  std::free(snapshot);
  scheduler->SetPaused(false);
  deadline = xe::Clock::QueryHostUptimeMillis() + 5000;
  for (;;) {
    bool all = true;
    for (unsigned i = 0; i < 6; ++i)
      all = all && xe::load_and_swap<uint32_t>(memory->TranslateVirtual(data + i * 16)) > saved_counts[i];
    if (all) break;
    CHECK(xe::Clock::QueryHostUptimeMillis() < deadline);
  }
  scheduler->SetPaused(true);
  CHECK(scheduler->WaitUntilPaused(2000000));
  for (unsigned i = 0; i < 6; ++i)
    xe::store_and_swap<uint32_t>(memory->TranslateVirtual(data + i * 16 + 4), 1);
  scheduler->SetPaused(false);
  for (auto& thread : threads) {
    CHECK(xe::threading::Wait(thread->wait_handle(), false, std::chrono::seconds(5)) == xe::threading::WaitResult::kSuccess);
    thread->ReleaseHandle();
    thread.reset();
  }
  emulator.Shutdown();
  std::puts("PASS: six PPC/libco CPUs save, retire, restore RAM/kernel/registers and resume with fresh host continuations");
}
