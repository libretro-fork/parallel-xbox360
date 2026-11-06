/* Borrowed synchronous descriptor request. Mutable budgets keep table order.
 * Prepare once before inspecting candidate pools. Requests/table stay immutable
 * until allocation returns. No storage allocation or request/record copying. */
#ifndef XE_CORE_VK_POOL_BUDGET_H
#define XE_CORE_VK_POOL_BUDGET_H
#include <stdint.h>
#include <vulkan/vulkan_core.h>
#include <retro_inline.h>
/* Keep the budget policy in the native adapter on MSVC. */
#if defined(_MSC_VER)
#define XE_POOL_BUDGET_INLINE __forceinline
#else
#define XE_POOL_BUDGET_INLINE INLINE
#endif
enum { XE_VK_BUDGET_FITS = 1, XE_VK_BUDGET_FULL = 2 };
typedef struct xe_vk_pool_budget {
  const VkDescriptorPoolSize *requests, *types;
  uint32_t count, type_count, indices[2];
} xe_vk_pool_budget;
static XE_POOL_BUDGET_INLINE uint32_t xe_vk_pool_budget_index(
    const VkDescriptorPoolSize *types, uint32_t count, VkDescriptorType type) {
  uint32_t i;
  if (count && types[0].type == type) return 0;
  if (count > 1 && types[1].type == type) return 1;
  for (i = 2; i < count; ++i) if (types[i].type == type) return i;
  return count;
}
/* Zero means the request needs a dedicated native pool. Type counts above two
 * retain generic support; only the common small request caches indices. */
static XE_POOL_BUDGET_INLINE int xe_vk_pool_budget_prepare(xe_vk_pool_budget *p,
    const VkDescriptorPoolSize *types, uint32_t type_count,
    const VkDescriptorPoolSize *requests, uint32_t count) {
  uint32_t i, j, value;
  p->requests = requests; p->types = types; p->count = count; p->type_count = type_count;
  if (!count) return 0;
  for (i = 0; i < count; ++i) {
    j = xe_vk_pool_budget_index(types, type_count, requests[i].type);
    value = requests[i].descriptorCount;
    if (j == type_count || value > types[j].descriptorCount) return 0;
    if (count <= 2) p->indices[i] = j;
  }
  return 1;
}
/* Prepare must have succeeded. Zero means insufficient descriptors/sets. Full
 * includes exact type exhaustion or the final set slot; don't reuse that pool. */
static XE_POOL_BUDGET_INLINE int xe_vk_pool_budget_fit(const xe_vk_pool_budget *p,
    const uint32_t *remaining, uint32_t sets) {
  uint32_t i, j, value, available;
  int result = XE_VK_BUDGET_FITS;
  if (!sets) return 0;
  if (sets == 1) result |= XE_VK_BUDGET_FULL;
  if (p->count <= 2) {
    value = p->requests[0].descriptorCount; available = remaining[p->indices[0]];
    if (value > available) return 0;
    if (value == available) result |= XE_VK_BUDGET_FULL;
    if (p->count == 2) {
      value = p->requests[1].descriptorCount; available = remaining[p->indices[1]];
      if (value > available) return 0;
      if (value == available) result |= XE_VK_BUDGET_FULL;
    }
    return result;
  }
  for (i = 0; i < p->count; ++i) {
    j = xe_vk_pool_budget_index(p->types, p->type_count, p->requests[i].type);
    value = p->requests[i].descriptorCount; available = remaining[j];
    if (value > available) return 0;
    if (value == available) result |= XE_VK_BUDGET_FULL;
  }
  return result;
}
/* Only after successful native allocation and fit; failed allocations do not
 * spend. The caller tracks set slots separately and discards full search nodes. */
static XE_POOL_BUDGET_INLINE void xe_vk_pool_budget_spend(const xe_vk_pool_budget *p, uint32_t *remaining) {
  uint32_t i, j;
  if (p->count <= 2) {
    remaining[p->indices[0]] -= p->requests[0].descriptorCount;
    if (p->count == 2) remaining[p->indices[1]] -= p->requests[1].descriptorCount;
    return;
  }
  for (i = 0; i < p->count; ++i) {
    j = xe_vk_pool_budget_index(p->types, p->type_count, p->requests[i].type);
    remaining[j] -= p->requests[i].descriptorCount;
  }
}
static XE_POOL_BUDGET_INLINE uint32_t xe_vk_pool_budget_maximum(const uint32_t *remaining, uint32_t count) {
  uint32_t i, maximum = 0;
  for (i = 0; i < count; ++i) if (remaining[i] > maximum) maximum = remaining[i];
  return maximum;
}
/* Only needed after hot/indexed pools fail; avoid extra hot-path planning. */
static XE_POOL_BUDGET_INLINE uint32_t xe_vk_pool_budget_request_maximum(const xe_vk_pool_budget *p) {
  uint32_t i, maximum = 0;
  for (i = 0; i < p->count; ++i)
    if (p->requests[i].descriptorCount > maximum) maximum = p->requests[i].descriptorCount;
  return maximum;
}
static XE_POOL_BUDGET_INLINE int xe_vk_pool_budget_new_full(const xe_vk_pool_budget *p, uint32_t sets) {
  uint32_t i, j;
  if (sets == 1) return 1;
  for (i = 0; i < p->count; ++i) {
    j = p->count <= 2 ? p->indices[i] : xe_vk_pool_budget_index(p->types, p->type_count, p->requests[i].type);
    if (p->requests[i].descriptorCount == p->types[j].descriptorCount) return 1;
  }
  return 0;
}
static XE_POOL_BUDGET_INLINE void xe_vk_pool_budget_initialize(const xe_vk_pool_budget *p, uint32_t *remaining) {
  uint32_t i;
  for (i = 0; i < p->type_count; ++i) remaining[i] = p->types[i].descriptorCount;
  xe_vk_pool_budget_spend(p, remaining);
}
#undef XE_POOL_BUDGET_INLINE
#endif
