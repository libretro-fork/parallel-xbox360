#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "core_vk_pool_budget.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static uint32_t state = 773111;
static uint32_t random_next(void) {
  state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state;
}
int main(void) {
  VkDescriptorPoolSize types[17], requests[17];
  uint32_t remaining[17], initialized[17], full_capacity[17];
  uint32_t capacity[17], available[17], expected[17], order[17];
  xe_vk_pool_budget plan;
  uint32_t trial, n, count, i, j, id, temporary, sets, maximum, exhausts;
  int normal, fit, expected_fit;
  CHECK(!xe_vk_pool_budget_prepare(&plan, NULL, 0, NULL, 0));
  for (trial = 0; trial < 300000; ++trial) {
    n = 1 + random_next() % 17;
    count = 1 + random_next() % n;
    if (trial % 3 == 0) count = 1;
    if (trial % 3 == 1 && n > 1) count = 2;
    for (i = 0; i < n; ++i) {
      capacity[i] = 1 + random_next() % 64;
      available[i] = random_next() % (capacity[i] + 1);
      order[i] = i;
    }
    for (i = n; i > 1; --i) {
      j = random_next() % i; temporary = order[j]; order[j] = order[i - 1]; order[i - 1] = temporary;
    }
    for (i = 0; i < n; ++i) {
      id = order[i];
      types[i].type = (VkDescriptorType)(100 + id); types[i].descriptorCount = capacity[id];
      remaining[i] = available[id]; full_capacity[i] = capacity[id];
    }
    for (i = n; i > 1; --i) {
      j = random_next() % i; temporary = order[j]; order[j] = order[i - 1]; order[i - 1] = temporary;
    }
    normal = 1; maximum = exhausts = 0; sets = random_next() % 8;
    expected_fit = sets ? XE_VK_BUDGET_FITS | (sets == 1 ? XE_VK_BUDGET_FULL : 0) : 0;
    for (i = 0; i < count; ++i) {
      id = order[i]; requests[i].type = (VkDescriptorType)(100 + id);
      requests[i].descriptorCount = 1 + random_next() % 70;
      if (requests[i].descriptorCount > capacity[id]) normal = 0;
      if (requests[i].descriptorCount == capacity[id]) exhausts = 1;
      if (requests[i].descriptorCount > maximum) maximum = requests[i].descriptorCount;
      if (requests[i].descriptorCount > available[id]) expected_fit = 0;
      else if (expected_fit && requests[i].descriptorCount == available[id]) expected_fit |= XE_VK_BUDGET_FULL;
    }
    if (!(trial % 19)) { requests[0].type = (VkDescriptorType)999; normal = 0; }
    CHECK(xe_vk_pool_budget_prepare(&plan, types, n, requests, count) == normal);
    if (!normal) continue;
    CHECK(plan.requests == requests && plan.types == types && xe_vk_pool_budget_request_maximum(&plan) == maximum && xe_vk_pool_budget_new_full(&plan, 8) == (exhausts != 0));
    fit = xe_vk_pool_budget_fit(&plan, remaining, sets);
    CHECK(fit == expected_fit);
    memcpy(expected, capacity, n * sizeof(expected[0]));
    for (i = 0; i < count; ++i) expected[(uint32_t)requests[i].type - 100] -= requests[i].descriptorCount;
    xe_vk_pool_budget_initialize(&plan, initialized);
    for (i = 0; i < n; ++i) {
      CHECK(initialized[i] == expected[(uint32_t)types[i].type - 100]);
    }
    if (fit) {
      memcpy(expected, available, n * sizeof(expected[0]));
      for (i = 0; i < count; ++i) expected[(uint32_t)requests[i].type - 100] -= requests[i].descriptorCount;
      xe_vk_pool_budget_spend(&plan, remaining);
      for (i = 0; i < n; ++i) {
        CHECK(remaining[i] == expected[(uint32_t)types[i].type - 100]);
      }
    }
    maximum = 0;
    for (i = 0; i < n; ++i) if (remaining[i] > maximum) maximum = remaining[i];
    CHECK(xe_vk_pool_budget_maximum(remaining, n) == maximum);
  }
  /* Exercise the complete generic request, not only large tables with two types. */
  for (i = 0; i < 17; ++i) {
    types[i].type = (VkDescriptorType)(100 + i); types[i].descriptorCount = 5;
    requests[16 - i].type = types[i].type; requests[16 - i].descriptorCount = 1;
  }
  CHECK(xe_vk_pool_budget_prepare(&plan, types, 17, requests, 17));
  for (i = 0; i < 17; ++i) full_capacity[i] = 5;
  CHECK(xe_vk_pool_budget_fit(&plan, full_capacity, 8) == XE_VK_BUDGET_FITS);
  xe_vk_pool_budget_initialize(&plan, initialized);
  for (i = 0; i < 17; ++i) CHECK(initialized[i] == 4);
  /* Exact type equality retires a pool even with set slots remaining. */
  types[0].type = VK_DESCRIPTOR_TYPE_SAMPLER; types[0].descriptorCount = 5;
  requests[0] = types[0]; full_capacity[0] = 5;
  CHECK(xe_vk_pool_budget_prepare(&plan, types, 1, requests, 1));
  CHECK(xe_vk_pool_budget_fit(&plan, full_capacity, 8) == (XE_VK_BUDGET_FITS | XE_VK_BUDGET_FULL));
  requests[0].descriptorCount = 1;
  CHECK(xe_vk_pool_budget_prepare(&plan, types, 1, requests, 1));
  CHECK(xe_vk_pool_budget_fit(&plan, full_capacity, 1) == (XE_VK_BUDGET_FITS | XE_VK_BUDGET_FULL));
  types[0].descriptorCount = full_capacity[0] = UINT32_MAX;
  requests[0].descriptorCount = UINT32_MAX;
  CHECK(xe_vk_pool_budget_prepare(&plan, types, 1, requests, 1));
  CHECK(xe_vk_pool_budget_fit(&plan, full_capacity, 8) == (XE_VK_BUDGET_FITS | XE_VK_BUDGET_FULL));
  xe_vk_pool_budget_initialize(&plan, initialized);
  CHECK(!initialized[0]);
  requests[0].descriptorCount = 1;
  CHECK(xe_vk_pool_budget_prepare(&plan, types, 1, requests, 1));
  xe_vk_pool_budget_initialize(&plan, initialized);
  CHECK(initialized[0] == UINT32_MAX - 1);
  puts("Pool budget C89: 300,000 reordered one/two/generic-type requests, borrowed plans, exhaustion and counter conservation pass");
  return 0;
}
