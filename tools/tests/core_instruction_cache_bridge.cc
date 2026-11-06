/* Actual mapped owner and module-address lookup, no JIT/backend/frontend. */
#include "xenia/cpu/xex_module.h"
#include "xenia/cpu/processor.h"
#include "xenia/base/filesystem.h"
#include <cstring>
using Module = xe::cpu::XexModule;
struct LowTag { using type = uint32_t Module::*; friend type ic_member(LowTag); };
struct HighTag { using type = uint32_t Module::*; friend type ic_member(HighTag); };
struct CacheTag { using type = xe::cpu::XexInfoCache Module::*; friend type ic_member(CacheTag); };
template<class Tag, typename Tag::type Member> struct IcMember {
  friend typename Tag::type ic_member(Tag) { return Member; }
};
template struct IcMember<LowTag, &Module::low_address_>;
template struct IcMember<HighTag, &Module::high_address_>;
template struct IcMember<CacheTag, &Module::info_cache_>;
static_assert(sizeof(xe::cpu::XexInfoCache::InfoCacheFlagsHeader) == XE_IC_HEADER_SIZE);
static_assert(sizeof(xe::cpu::InfoCacheFlags) == 4);
extern "C" void xe_test_prepare_delta(int, int, const uint8_t**, size_t*,
    const uint8_t**, size_t*, const uint8_t**);
extern "C" int xe_test_ic_mapping(const char* path, size_t size) {
  const uint8_t *base, *patch, *unused;
  size_t base_length, patch_length;
  xe_test_prepare_delta(0, 0, &base, &base_length, &patch, &patch_length, &unused);
  xe::Memory memory;
  if (!memory.Initialize()) return 0;
  xe::cpu::Processor processor(&memory, nullptr);
  {
    Module module(&processor, nullptr);
    if (!module.Load("fixture", "fixture", base, base_length)) return 0;
    auto& cache = module.*ic_member(CacheTag{});
    if (cache.LookupFlags(0)) return 0;
    cache.executable_addr_flags_ = xe::MappedMemory::Open(xe::to_path(path),
        xe::MappedMemory::Mode::kReadWrite, 0, size);
    if (!cache.executable_addr_flags_ || cache.executable_addr_flags_->size() != size ||
        !xe_ic_initialize(cache.executable_addr_flags_->data(),size,4)) return 0;
    module.*ic_member(LowTag{}) = 0x82001000;
    module.*ic_member(HighTag{}) = 0x82001008;
    auto* first = module.GetInstructionAddressFlags(0x82001000);
    if (!first || first != cache.LookupFlags(0) || !cache.LookupFlags(4) ||
        module.GetInstructionAddressFlags(0x82001008) ||
        module.GetInstructionAddressFlags(0x82000fff) || cache.LookupFlags(8) ||
        cache.LookupFlags(UINT32_MAX)) return 0;
    xe_ic_flags_set(first, XE_IC_FLAG_RESOLVED | XE_IC_FLAG_RETURN_SITE);
    cache.executable_addr_flags_->Flush();
    if (!module.Unload() || module.GetInstructionAddressFlags(0x82001000)) return 0;
  }
  int fresh;
  if (!xe_ic_prepare(path,size,4,&fresh) || fresh) return 0;
  auto mapping = xe::MappedMemory::Open(xe::to_path(path),xe::MappedMemory::Mode::kReadWrite,0,size);
  if (!mapping) return 0;
  uint32_t stored = 0;
  std::memcpy(&stored,xe_ic_slot(mapping->data(),mapping->size(),0),4);
  return (stored & (XE_IC_FLAG_RESOLVED | XE_IC_FLAG_RETURN_SITE)) ==
      (XE_IC_FLAG_RESOLVED | XE_IC_FLAG_RETURN_SITE);
}
