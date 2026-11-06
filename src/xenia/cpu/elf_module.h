/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_ELF_MODULE_H_
#define XENIA_CPU_ELF_MODULE_H_

#include <string>

#include "xenia/cpu/module.h"

namespace xe {
namespace kernel {
class KernelState;
}  // namespace kernel

namespace cpu {

// ELF module: Used to load libxenon executables.
class ElfModule : public xe::cpu::Module {
 public:
  ElfModule(Processor* processor, kernel::KernelState* kernel_state);
  virtual ~ElfModule();

  bool loaded() const { return loaded_; }
  uint32_t entry_point() const { return entry_point_; }
  const std::string& name() const override { return name_; }
  bool is_executable() const override;
  const std::string& path() const { return path_; }

  bool Load(const std::string_view name, const std::string_view path,
            const void* elf_addr, size_t elf_length, bool initial_title = false);
  bool Unload();

 protected:
  std::unique_ptr<Function> CreateFunction(uint32_t address) override;

 private:
  std::string name_;
  std::string path_;

  bool loaded_ = false;
  uint32_t entry_point_ = 0;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_ELF_MODULE_H_
