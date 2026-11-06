#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
static size_t calls, frees, allocations, fail_at;
static void *alloc_test(size_t n) {
  void *p;
  if (++calls == fail_at) return NULL;
  p = malloc(n); if (p) ++allocations; return p;
}
static void free_test(void *p) { if (p) ++frees; free(p); }
#define XE_POOL_OWNER_ALLOC alloc_test
#define XE_POOL_OWNER_FREE free_test
#include "core_vk_pool_owner.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static size_t destroyed, expected;
static int destroy_error;
static VKAPI_ATTR void VKAPI_CALL destroy_pool(VkDevice device, VkDescriptorPool pool, const VkAllocationCallbacks *callbacks) {
  if (device != (VkDevice)(uintptr_t)1 || callbacks || (uintptr_t)pool != ++expected) destroy_error = 1;
  ++destroyed;
}
int main(void) {
  xe_vk_pool_owner s = {0};
  xe_vk_pool_record *r, *first;
  VkDescriptorPoolSize *counts, *first_counts;
  size_t t;
  size_t i, epoch, before = 0;
  CHECK(!xe_vk_pool_owner_configure(&s, (size_t)-1));
  CHECK(xe_vk_pool_owner_configure(&s, 3));
  CHECK(!xe_vk_pool_owner_counts(&s));
  fail_at = 1;
  CHECK(!xe_vk_pool_owner_warm(&s) && !s.pages);
  fail_at = 0;
  CHECK(xe_vk_pool_owner_warm(&s));
  CHECK(!xe_vk_pool_owner_configure(&s, 17));
  first = xe_vk_pool_owner_acquire(&s);
  first_counts = xe_vk_pool_owner_counts(&s);
  CHECK(first_counts);
  for (t = 0; t < 3; ++t) {
    first_counts[t].type = VK_DESCRIPTOR_TYPE_SAMPLER;
    first_counts[t].descriptorCount = (uint32_t)t + 9;
  }
  CHECK(first && !xe_vk_pool_owner_acquire(&s));
  CHECK(!xe_vk_pool_owner_commit(&s));
  CHECK(!xe_vk_pool_owner_reset(&s, (VkDevice)(uintptr_t)1, destroy_pool));
  CHECK(!xe_vk_pool_owner_destroy(&s));
  xe_vk_pool_owner_cancel(&s);
  CHECK(xe_vk_pool_owner_acquire(&s) == first);
  first->pool = (VkDescriptorPool)(uintptr_t)1;
  CHECK(xe_vk_pool_owner_commit(&s));
  for (i = 1; i < 64; ++i) {
    r = xe_vk_pool_owner_acquire(&s); CHECK(r);
    r->pool = (VkDescriptorPool)(uintptr_t)(i + 1); CHECK(xe_vk_pool_owner_commit(&s));
  }
  fail_at = calls + 1;
  CHECK(!xe_vk_pool_owner_acquire(&s) && s.count == 64 && !s.leased);
  fail_at = 0;
  r = xe_vk_pool_owner_acquire(&s); CHECK(r);
  xe_vk_pool_owner_cancel(&s);
  CHECK(s.count == 64 && first->pool == (VkDescriptorPool)(uintptr_t)1);
  CHECK(first_counts[2].descriptorCount == 11);
  CHECK(xe_vk_pool_owner_reset(&s, (VkDevice)(uintptr_t)1, destroy_pool));
  CHECK(destroyed == 64 && !destroy_error);
  for (epoch = 0; epoch < 1001; ++epoch) {
    expected = 0;
    if (epoch == 1) before = calls;
    for (i = 0; i < 257; ++i) {
      r = xe_vk_pool_owner_acquire(&s); CHECK(r);
      counts = xe_vk_pool_owner_counts(&s); CHECK(counts);
      if (!i) CHECK(r == first && counts == first_counts);
      for (t = 0; t < 3; ++t) {
        counts[t].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        counts[t].descriptorCount = (uint32_t)(i + t);
      }
      r->pool = (VkDescriptorPool)(uintptr_t)(i + 1); CHECK(xe_vk_pool_owner_commit(&s));
    }
    CHECK(first_counts[0].descriptorCount == 0 && first_counts[2].descriptorCount == 2);
    CHECK(xe_vk_pool_owner_reset(&s, (VkDevice)(uintptr_t)1, destroy_pool));
    CHECK(!destroy_error && expected == 257);
  }
  CHECK(before == calls);
  CHECK(xe_vk_pool_owner_destroy(&s) && xe_vk_pool_owner_destroy(&s));
  CHECK(allocations == frees);
  CHECK(xe_vk_pool_owner_configure(&s, 17));
  r = xe_vk_pool_owner_acquire(&s); CHECK(r);
  counts = xe_vk_pool_owner_counts(&s); CHECK(counts);
  for (t = 0; t < 17; ++t) {
    counts[t].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    counts[t].descriptorCount = (uint32_t)t;
  }
  CHECK(counts[16].descriptorCount == 16);
  xe_vk_pool_owner_cancel(&s);
  CHECK(xe_vk_pool_owner_destroy(&s) && allocations == frees);
  puts("Pool ownership: 257,000 retained-record lifetimes, growth failure, cancel, lease gates and exact-once destruction pass");
  return 0;
}
