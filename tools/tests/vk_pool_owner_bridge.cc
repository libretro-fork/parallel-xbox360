/* C++ boundary fixture: compile the real allocator with fake Vulkan dispatch,
 * sweep throwing C++ allocations and logger failures. Core storage is C89. */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include "xenia/ui/vulkan/linked_type_descriptor_set_allocator.h"
static size_t new_calls, fail_new, log_calls, fail_log, metadata_calls, fail_metadata;
void *xe_test_pool_metadata_alloc(size_t bytes) {
  if (++metadata_calls == fail_metadata) return nullptr;
  return std::malloc(bytes);
}
void* operator new(size_t n) {
  if (++new_calls == fail_new) throw std::bad_alloc();
  if (void* p = std::malloc(n ? n : 1)) return p;
  throw std::bad_alloc();
}
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
void test_vk_pool_log() { if (++log_calls == fail_log) throw std::bad_alloc(); }
static uint64_t trace;
static void trace_value(uint64_t value) { trace = (trace ^ value) * UINT64_C(1099511628211); }
static int native_errors = 1, hot_workload;
static unsigned char alive[4096];
static size_t created, destroyed, create_calls, allocate_calls, sets, bad;
static VKAPI_ATTR VkResult VKAPI_CALL create_pool(VkDevice, const VkDescriptorPoolCreateInfo* info,
    const VkAllocationCallbacks*, VkDescriptorPool* pool) {
  trace_value(1); trace_value(info->maxSets); trace_value(info->poolSizeCount);
  for (uint32_t i = 0; i < info->poolSizeCount; ++i) {
    trace_value(info->pPoolSizes[i].type); trace_value(info->pPoolSizes[i].descriptorCount);
  }
  ++create_calls;
  if (native_errors && create_calls % 11 == 0) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
  if (!info->maxSets || !info->poolSizeCount || created + 1 >= sizeof(alive)) std::abort();
  *pool = (VkDescriptorPool)(uintptr_t)++created;
  alive[created] = 1; return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL destroy_pool(VkDevice, VkDescriptorPool pool, const VkAllocationCallbacks*) {
  size_t id = (size_t)(uintptr_t)pool;
  if (!id || id > created || !alive[id]) { ++bad; return; }
  alive[id] = 0; ++destroyed;
}
static VKAPI_ATTR VkResult VKAPI_CALL allocate_sets(VkDevice, const VkDescriptorSetAllocateInfo* info, VkDescriptorSet* set) {
  size_t id = (size_t)(uintptr_t)info->descriptorPool;
  trace_value(2); trace_value(id);
  if (!id || id > created || !alive[id] || info->descriptorSetCount != 1) std::abort();
  ++allocate_calls;
  if (native_errors && allocate_calls % 7 == 0) return VK_ERROR_OUT_OF_POOL_MEMORY;
  *set = (VkDescriptorSet)(uintptr_t)++sets; return VK_SUCCESS;
}
static int run(size_t fail_allocation, size_t fail_logging, size_t* calls, size_t* logs) {
  using namespace xe::ui::vulkan;
  std::memset(alive, 0, sizeof(alive));
  created = destroyed = create_calls = allocate_calls = sets = bad = 0;
  trace = UINT64_C(14695981039346656037);
  new_calls = log_calls = metadata_calls = 0; fail_new = fail_allocation; fail_log = fail_logging;
  VulkanDevice::Functions functions = {create_pool, destroy_pool, allocate_sets};
  VulkanDevice device(functions);
  VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 32}, {VK_DESCRIPTOR_TYPE_SAMPLER, 16}};
  if (hot_workload) sizes[0].descriptorCount = sizes[1].descriptorCount = 65536;
  try {
    LinkedTypeDescriptorSetAllocator allocator(&device, sizes, 2, hot_workload ? 16384 : 8);
    for (size_t epoch = 0; epoch < 3; ++epoch) {
      for (uint32_t i = 0; i < 500; ++i) {
        VkDescriptorPoolSize request[2] = {
          {i % 19 ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
           i % 17 ? (i % 2 ? 1u : 21u) : 50u},
          {VK_DESCRIPTOR_TYPE_SAMPLER, i % 2 ? 11u : 1u}};
        if (hot_workload) {
          request[0].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
          request[0].descriptorCount = request[1].descriptorCount = 1;
        }
        try {
          allocator.Allocate((VkDescriptorSetLayout)(uintptr_t)1, request, 2);
        } catch (const std::bad_alloc&) {
          fail_new = fail_log = fail_metadata = 0;
          allocator.Allocate((VkDescriptorSetLayout)(uintptr_t)1, request, 2);
        }
      }
      allocator.Reset();
      if (created != destroyed) ++bad;
    }
  } catch (const std::bad_alloc&) {}
  fail_new = fail_log = fail_metadata = 0;
  if (calls) *calls = new_calls;
  if (logs) *logs = log_calls;
  if (bad || created != destroyed) {
    std::fprintf(stderr, "leak/double destroy: fail new=%zu log=%zu created=%zu destroyed=%zu bad=%zu\n",
        fail_allocation, fail_logging, created, destroyed, bad); return 1;
  }
  return 0;
}
int main() {
  size_t calls, logs, metadata;
  if (run(0, 0, &calls, &logs)) return 1;
  metadata = metadata_calls;
  if (calls || !metadata || !logs) {
    std::fprintf(stderr, "unexpected C++ allocation or vacuous failure fixture\n");
    return 1;
  }
  if (trace != UINT64_C(0x1aed303f01c48a25) || create_calls != 970 ||
      allocate_calls != 1478 || sets != 1267) {
    std::fprintf(stderr, "native pool-selection trace differs from 349a0b5e4\n");
    return 1;
  }
  std::printf("Native allocation trace: %016llx, creates=%zu allocations=%zu sets=%zu\n",
      (unsigned long long)trace, create_calls, allocate_calls, sets);
  for (size_t i = 1; i <= calls; ++i) if (run(i, 0, nullptr, nullptr)) return 1;
  for (size_t i = 1; i <= logs; ++i) if (run(0, i, nullptr, nullptr)) return 1;
  for (size_t i = 1; i <= metadata; ++i) {
    fail_metadata = i; if (run(0, 0, nullptr, nullptr)) return 1;
  }
  std::printf("Real allocator fake dispatch: %zu C++ allocation failures, %zu C89 metadata failures and %zu logger failures swept with retries; all native pools destroyed exactly once\n", calls, metadata, logs);
  return 0;
}
