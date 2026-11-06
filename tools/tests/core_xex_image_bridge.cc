/* Actual module/parser interop; all fixtures/algorithms are C89. */
#include "xenia/cpu/xex_module.h"
#include "xenia/cpu/processor.h"
#include "xenia/base/logging.h"
#include <cstring>
extern "C" int xe_test_xex_module(const uint8_t* source, size_t length,
    const uint8_t* expected, size_t size, int should_load) {
  xe::Memory memory;
  if (!memory.Initialize()) return 0;
  xe::cpu::Processor processor(&memory, nullptr);
  xe::cpu::XexModule module(&processor, nullptr);
  if (module.loaded() || module.is_executable() || module.image_size() ||
      module.LoadContinue() || module.ApplyPatch(nullptr) != 1) return 0;
  const bool loaded = module.Load("fixture", "fixture", source, length);
  if (!should_load) return !loaded;
  if (!loaded || !module.loaded() || !module.is_executable() ||
      module.image_size() != size ||
      std::memcmp(memory.TranslateVirtual(0x82000000), expected, size)) return 0;
  if (!module.Unload() || module.loaded() || module.xex_header() ||
      module.LoadContinue() || module.ApplyPatch(&module) != 1) return 0;
  return 1;
}
extern "C" int xe_test_xex_patch_without_descriptor(const uint8_t* source, size_t size) {
  xe::Memory memory;
  if (!memory.Initialize()) return 0;
  xe::cpu::Processor processor(&memory, nullptr);
  xe::cpu::XexModule patch(&processor, nullptr), target(&processor, nullptr);
  if (!patch.Load("patch", "patch", source, size) || !patch.is_patch() ||
      !target.Load("target", "target", source, size)) return 0;
  if (patch.ApplyPatch(&target) != 2) return 0;
  return patch.Unload() && target.Unload();
}
