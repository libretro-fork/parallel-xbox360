/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/base/threading.h"

#if XE_PLATFORM_WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "libretro/core_fiber_context.h"

namespace xe {
namespace threading {

namespace {

size_t HostPageSize() {
#if XE_PLATFORM_WIN32
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  return static_cast<size_t>(si.dwPageSize);
#else
  return static_cast<size_t>(sysconf(_SC_PAGESIZE));
#endif
}

// A guard-paged fiber stack: the lowest (floor) page is never usable so an
// overflow faults instead of corrupting adjacent memory. Windows commits an
// initial window and grows on guard faults, POSIX mmap commits lazily.
struct FiberStack {
  void* base = nullptr;  // low address of the whole allocation
  size_t total = 0;      // whole allocation, including the floor page
  void* high = nullptr;  // one past the top, aligned for libco initialization
  size_t usable = 0;     // total minus the floor page
};

// Live stacks, for growth and for classifying a fault address as an overflow.
// Faults are rare, a mutex and linear scan are fine.
struct StackRecord {
  uintptr_t base = 0;  // reserve low, the floor page
  size_t total = 0;
  uintptr_t guard = 0;  // current PAGE_GUARD page, base when fully grown
};
std::mutex stack_registry_lock_;
std::vector<StackRecord> stack_registry_;

#if XE_PLATFORM_WIN32
// Committed up front at the top, then grown per guard fault.
constexpr size_t kInitialCommitBytes = 64 * 1024;
constexpr size_t kGrowCommitBytes = 256 * 1024;

// Grows a registered fiber stack on a guard fault and reports the overflow
// when one reaches the floor page. An alloca can jump past the guard into
// reserved pages, which arrives as an access violation instead.
LONG NTAPI FiberStackExceptionHandler(PEXCEPTION_POINTERS info) {
  const DWORD code = info->ExceptionRecord->ExceptionCode;
  if (code != STATUS_GUARD_PAGE_VIOLATION && code != STATUS_ACCESS_VIOLATION) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  const uintptr_t addr =
      static_cast<uintptr_t>(info->ExceptionRecord->ExceptionInformation[1]);
  const size_t page = HostPageSize();
  std::lock_guard<std::mutex> lock(stack_registry_lock_);
  for (auto& r : stack_registry_) {
    if (addr < r.base || addr >= r.base + r.total) {
      continue;
    }
    const uintptr_t floor_end = r.base + page;
    if (addr < floor_end || r.guard <= r.base) {
      // Exhausted. Commit the floor so the report itself has stack to run on,
      // then let the fault propagate as a crash.
      VirtualAlloc(reinterpret_cast<void*>(r.base), page, MEM_COMMIT,
                   PAGE_READWRITE);
      XELOGE(
          "Fiber stack overflow: {} KiB stack exhausted, fault at {:X} (host "
          "tid {:08X})",
          r.total / 1024, addr, GetCurrentThreadId());
      return EXCEPTION_CONTINUE_SEARCH;
    }
    // Commit down to the faulting page plus a chunk of headroom and re-arm
    // the guard below that.
    uintptr_t fault_page = addr & ~static_cast<uintptr_t>(page - 1);
    uintptr_t new_low =
        fault_page > kGrowCommitBytes ? fault_page - kGrowCommitBytes : 0;
    if (new_low < floor_end) {
      new_low = floor_end;
    }
    uintptr_t commit_end = (r.guard > fault_page ? r.guard : fault_page) + page;
    VirtualAlloc(reinterpret_cast<void*>(new_low), commit_end - new_low,
                 MEM_COMMIT, PAGE_READWRITE);
    // Strip a bypassed guard page's flag so it cannot fire mid-stack later.
    DWORD old_protect;
    VirtualProtect(reinterpret_cast<void*>(r.guard), page, PAGE_READWRITE,
                   &old_protect);
    if (new_low > floor_end) {
      uintptr_t new_guard = new_low - page;
      VirtualAlloc(reinterpret_cast<void*>(new_guard), page, MEM_COMMIT,
                   PAGE_READWRITE | PAGE_GUARD);
      r.guard = new_guard;
    } else {
      // Fully grown, the next stop is the floor.
      r.guard = r.base;
    }
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

// Protected by stack_registry_lock_. The callback must not outlive the last
// stack: the frontend can unload this DLL after closing content.
void* stack_veh_handle_ = nullptr;
#endif  // XE_PLATFORM_WIN32

FiberStack AllocFiberStack(size_t size) {
  const size_t page = HostPageSize();
  size_t pages = (size + page - 1) / page;
  if (pages < 1) {
    pages = 1;
  }
  FiberStack s;
  s.total = (pages + 1) * page;  // +1 floor page at the low end
  StackRecord record;

#if XE_PLATFORM_WIN32
  s.base = VirtualAlloc(nullptr, s.total, MEM_RESERVE, PAGE_READWRITE);
  if (!s.base) {
    xe::FatalError("Failed to reserve fiber stack");
  }
  char* top = static_cast<char*>(s.base) + s.total;
  size_t initial = kInitialCommitBytes;
  // Small stack: commit everything above the floor, no growth.
  if (initial + 2 * page >= s.total) {
    initial = s.total - page;
    VirtualAlloc(top - initial, initial, MEM_COMMIT, PAGE_READWRITE);
    record.guard = reinterpret_cast<uintptr_t>(s.base);
  } else {
    VirtualAlloc(top - initial, initial, MEM_COMMIT, PAGE_READWRITE);
    char* guard = top - initial - page;
    VirtualAlloc(guard, page, MEM_COMMIT, PAGE_READWRITE | PAGE_GUARD);
    record.guard = reinterpret_cast<uintptr_t>(guard);
  }
#else
  s.base = mmap(nullptr, s.total, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (s.base == MAP_FAILED) {
    xe::FatalError("Failed to map fiber stack");
  }
  mprotect(s.base, page, PROT_NONE);
  record.guard = reinterpret_cast<uintptr_t>(s.base);
#endif

  s.high = static_cast<char*>(s.base) + s.total;
  s.usable = s.total - page;
  record.base = reinterpret_cast<uintptr_t>(s.base);
  record.total = s.total;
  {
    std::lock_guard<std::mutex> lock(stack_registry_lock_);
    stack_registry_.push_back(record);
#if XE_PLATFORM_WIN32
    // Install before publishing the stack to its owner, and reinstall when
    // content is loaded again after all previous stacks were released.
    if (!stack_veh_handle_) {
      stack_veh_handle_ =
          AddVectoredExceptionHandler(1, FiberStackExceptionHandler);
      if (!stack_veh_handle_) {
        xe::FatalError("Failed to install fiber stack exception handler");
      }
    }
#endif
  }
  return s;
}

void FreeFiberStack(FiberStack& s) {
  if (!s.base) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(stack_registry_lock_);
    for (auto it = stack_registry_.begin(); it != stack_registry_.end(); ++it) {
      if (it->base == reinterpret_cast<uintptr_t>(s.base)) {
        stack_registry_.erase(it);
        break;
      }
    }
#if XE_PLATFORM_WIN32
    if (stack_registry_.empty() && stack_veh_handle_) {
      if (!RemoveVectoredExceptionHandler(stack_veh_handle_)) {
        xe::FatalError("Failed to remove fiber stack exception handler");
      }
      stack_veh_handle_ = nullptr;
    }
#endif
  }
#if XE_PLATFORM_WIN32
  VirtualFree(s.base, 0, MEM_RELEASE);
#else
  munmap(s.base, s.total);
#endif
  s = FiberStack{};
}

class LibcoFiber : public Fiber {
 public:
  LibcoFiber(size_t stack_size, std::function<void()> start_routine)
      : entry_(std::move(start_routine)) {
    context_.owner = this;
    stack_ = AllocFiberStack(stack_size);
    ResetContext();
    owns_stack_ = true;
  }

  LibcoFiber() {
    context_.owner = this;
    xe_fiber_context_adopt(&context_);
  }

  ~LibcoFiber() override {
    xe_fiber_context_destroy(&context_);
    if (owns_stack_) {
      FreeFiberStack(stack_);
    }
  }

  void SwitchTo() override {
    assert_not_null(GetCurrentFiber());
    assert_true(GetCurrentFiber() != this);
    assert_false(context_.finished);
    xe_fiber_context_switch(&context_);
  }

  void SetTerminated() override { context_.finished = 1; }

  void Restart() override {
    assert_true(owns_stack_);
    assert_true(GetCurrentFiber() != this);
    ResetContext();
  }

 private:
  void ResetContext() {
    const int initialized = xe_fiber_context_reset(&context_, stack_.high,
        static_cast<char*>(stack_.high) - stack_.usable,
        &LibcoFiber::EntryTrampoline);
    assert_true(initialized != 0);
    (void)initialized;
  }

  static void EntryTrampoline() {
    auto* self = static_cast<LibcoFiber*>(GetCurrentFiber());
    self->entry_();
  }

  std::function<void()> entry_;
  xe_fiber_context context_{};
  FiberStack stack_;
  bool owns_stack_ = false;
};

}  // namespace

std::unique_ptr<Fiber> Fiber::Create(CreationParameters params,
                                     std::function<void()> start_routine) {
  return std::make_unique<LibcoFiber>(params.stack_size,
                                         std::move(start_routine));
}

std::unique_ptr<Fiber> Fiber::CreateFromThread() {
  auto fiber = std::make_unique<LibcoFiber>();
  return fiber;
}

Fiber* Fiber::GetCurrentFiber() {
  return static_cast<Fiber*>(xe_fiber_context_current_owner());
}

void Fiber::ShutdownStackHandling() {
#if XE_PLATFORM_WIN32
  std::lock_guard<std::mutex> lock(stack_registry_lock_);
  if (stack_veh_handle_) {
    if (!RemoveVectoredExceptionHandler(stack_veh_handle_)) {
      xe::FatalError("Failed to detach fiber stack exception handler");
    }
    stack_veh_handle_ = nullptr;
  }
  if (!stack_registry_.empty()) {
    XELOGE("Fiber stack handling stopped with {} retired stack owners",
           stack_registry_.size());
  }
#endif
}

bool Fiber::IsStackOverflowFault(const void* address) {
  auto addr = reinterpret_cast<uintptr_t>(address);
  const size_t page = HostPageSize();
  std::lock_guard<std::mutex> lock(stack_registry_lock_);
  for (auto& r : stack_registry_) {
    if (addr >= r.base && addr < r.base + page) {
      return true;
    }
  }
  return false;
}

}  // namespace threading
}  // namespace xe
