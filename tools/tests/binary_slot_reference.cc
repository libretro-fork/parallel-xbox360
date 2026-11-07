/* Archived cache-operation adapter only; never runtime code.
 * Mirrors the previous IsGuestVertexReady mutex plus two exact-key map finds.
 * Scenarios, timing and checksums are in the C89 driver. */
#include "libretro/core_binary_slot.h"
#include <cstdint>
#include <mutex>
#include <unordered_map>
#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif
static std::mutex reference_mutex;
static std::unordered_map<uint64_t,std::unordered_map<uint64_t,void*>> reference_cache;
static xe_binary_slot slots[2048];
static unsigned marker;
extern "C" void xe_binary_reference_init(void) {
  for(unsigned i=0;i<2048;++i) {
    void* value=(i%11)?&marker:nullptr;
    reference_cache[i>>2].emplace(i&3,value);
    xe_binary_slot_init(slots+i);xe_binary_slot_publish(slots+i,value);
  }
}
extern "C" NOINLINE int xe_binary_reference_old(unsigned index) {
  std::lock_guard<std::mutex> lock(reference_mutex);
  auto shader=reference_cache.find(index>>2);
  if(shader==reference_cache.end())return false;
  auto binary=shader->second.find(index&3);
  return binary!=shader->second.end() && binary->second!=nullptr;
}
extern "C" NOINLINE int xe_binary_reference_slot(unsigned index) {
  return xe_binary_slot_read(slots+index).binary!=nullptr;
}
