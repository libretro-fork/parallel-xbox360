/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/elf_module.h"

#include "libretro/core_elf.h"
#include "xenia/base/logging.h"
#include "xenia/cpu/processor.h"

namespace xe {
namespace cpu {

ElfModule::ElfModule(Processor* processor, kernel::KernelState*)
    : Module(processor) {}

ElfModule::~ElfModule() = default;

bool ElfModule::is_executable() const { return loaded_ && entry_point_ != 0; }

bool ElfModule::Load(const std::string_view name, const std::string_view path,
                     const void* elf_addr, size_t elf_length, bool initial_title) {
  if (loaded_) return false;
  xe_elf_plan plan;
  if (!xe_elf_validate(&plan, elf_addr, elf_length,
          memory()->LookupHeap(0x80000000)->page_size(),
          memory()->LookupHeap(0x90000000)->page_size())) {
    XELOGE("ELF: Invalid or unsupported load layout");
    return false;
  }
  name_ = name;
  path_ = path;
  struct Context { ElfModule* module; uint32_t boot_end; } context{this, 0};
  // Memory::Initialize lends the first 256 KiB as a boot placeholder. Libxenon
  // links at 0x80000000; retain that backing only for the initial title, after
  // verifying its exact region. Other live allocations are never overwritten.
  HeapAllocationInfo boot{};
  if (initial_title && memory()->LookupHeap(0x80000000)->QueryRegionInfo(
          0x80000000, &boot, 0x40000) &&
      boot.allocation_base == 0x80000000 && boot.allocation_size == 0x40000 &&
      boot.region_size == 0x40000 &&
      boot.state == (kMemoryAllocationReserve | kMemoryAllocationCommit) &&
      boot.protect == (kMemoryProtectRead | kMemoryProtectWrite))
    context.boot_end = 0x80040000;
  const xe_elf_sink sink = {
      &context,
      [](void* opaque, uint32_t base, uint32_t size) -> int {
        auto* context = static_cast<Context*>(opaque);
        if (base < context->boot_end) {
          if (size <= context->boot_end - base) return 1;
          size -= context->boot_end - base; base = context->boot_end;
        }
        auto* module = context->module;
        auto* heap = module->memory()->LookupHeap(base);
        uint32_t actual = 0;
        if (!heap || !heap->AllocRange(base, base + size - 1, size,
                heap->page_size(), kMemoryAllocationReserve | kMemoryAllocationCommit,
                kMemoryProtectRead | kMemoryProtectWrite, false, &actual)) return 0;
        if (actual == base) return 1;
        heap->Release(actual);
        return 0;
      },
      [](void* opaque, uint32_t base, uint32_t size) {
        auto* context = static_cast<Context*>(opaque);
        if (base < context->boot_end) {
          if (size <= context->boot_end - base) return;
          base = context->boot_end;
        }
        context->module->memory()->LookupHeap(base)->Release(base);
      },
      [](void* opaque, uint32_t address) -> uint8_t* {
        return static_cast<Context*>(opaque)->module->memory()->TranslateVirtual(address);
      },
      [](void* opaque, uint32_t base, uint32_t end) {
        static_cast<Context*>(opaque)->module->processor_->backend()->CommitExecutableRange(base, end);
      }};
  // C state also remains visible to this guard if a virtual callback throws.
  struct Lease {
    const xe_elf_plan* plan;
    const xe_elf_sink* sink;
    xe_elf_state state{};
    ~Lease() { xe_elf_rollback(plan, sink, &state); }
  } lease{&plan, &sink};
  if (!xe_elf_apply(&plan, &sink, &lease.state)) {
    XELOGE("ELF: Unable to reserve or translate guest load ranges");
    return false;
  }
  entry_point_ = plan.entry;
  loaded_ = true;
  return true;
}
std::unique_ptr<Function> ElfModule::CreateFunction(uint32_t address) {
  return std::unique_ptr<Function>(
      processor_->backend()->CreateGuestFunction(this, address));
}

}  // namespace cpu
}  // namespace xe
