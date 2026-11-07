/* Immutable native descriptors and byte-for-byte source provenance, MSVC C89. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_vk_source_views.h"
#include "core_gpu_resolve_view.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x); exit(1); } } while (0)
static VkDevice device;
static unsigned creates, allocates, writes, destroys, live, fail_create, fail_allocate, missing;
static uint32_t expected_count;
static uint64_t expected_size;
static VkDescriptorSetLayout layout;
static VkBuffer buffer_at(void *context, uint32_t index) {
  CHECK(context == &live);
  return index == missing ? VK_NULL_HANDLE : (VkBuffer)(uintptr_t)(100 + index);
}
static VkResult VKAPI_CALL create_pool(VkDevice dev, const VkDescriptorPoolCreateInfo *info,
    const VkAllocationCallbacks *callbacks, VkDescriptorPool *pool) {
  CHECK(dev == device && !callbacks && !info->pNext && !info->flags);
  CHECK(info->sType == VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO && info->maxSets == expected_count);
  CHECK(info->poolSizeCount == 1 && info->pPoolSizes->type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER &&
      info->pPoolSizes->descriptorCount == expected_count);
  ++creates; if (fail_create) return VK_ERROR_OUT_OF_POOL_MEMORY;
  CHECK(!live); live = 1; *pool = (VkDescriptorPool)(uintptr_t)7; return VK_SUCCESS;
}
static void VKAPI_CALL destroy_pool(VkDevice dev, VkDescriptorPool pool, const VkAllocationCallbacks *callbacks) {
  CHECK(dev == device && !callbacks && (uintptr_t)pool == 7 && live == 1);
  ++destroys; live = 0;
}
static VkResult VKAPI_CALL allocate_sets(VkDevice dev, const VkDescriptorSetAllocateInfo *info, VkDescriptorSet *sets) {
  uint32_t i;
  CHECK(dev == device && live && !info->pNext && (uintptr_t)info->descriptorPool == 7);
  CHECK(info->sType == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO && info->descriptorSetCount == expected_count);
  ++allocates;
  for (i = 0; i < expected_count; ++i) {
    CHECK(info->pSetLayouts[i] == layout); sets[i] = (VkDescriptorSet)(uintptr_t)(1000 + i);
  }
  return fail_allocate ? VK_ERROR_OUT_OF_POOL_MEMORY : VK_SUCCESS;
}
static void VKAPI_CALL update_sets(VkDevice dev, uint32_t count, const VkWriteDescriptorSet *data,
    uint32_t copies, const VkCopyDescriptorSet *copy) {
  uint32_t i;
  CHECK(dev == device && live && count == expected_count && !copies && !copy); ++writes;
  for (i = 0; i < count; ++i) {
    CHECK(data[i].sType == VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET && !data[i].pNext);
    CHECK((uintptr_t)data[i].dstSet == 1000 + i && !data[i].dstBinding && !data[i].dstArrayElement);
    CHECK(data[i].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER && data[i].descriptorCount == 1);
    CHECK(!data[i].pImageInfo && !data[i].pTexelBufferView);
    CHECK((uintptr_t)data[i].pBufferInfo->buffer == 100 + i && !data[i].pBufferInfo->offset &&
        data[i].pBufferInfo->range == expected_size);
  }
}
static xe_vk_source_functions functions = {create_pool, destroy_pool, allocate_sets, update_sets};
static void bank_tests(void) {
  xe_vk_source_bank bank;
  uint32_t count, i;
  unsigned before_creates, before_allocates, before_writes;
  memset(&bank, 0, sizeof(bank));
  missing = UINT32_MAX; expected_size = UINT64_C(2) << 30;
  CHECK(!xe_vk_source_bank_init(&bank, device, &functions, layout, 0, expected_size, expected_size, buffer_at, &live));
  CHECK(!xe_vk_source_bank_init(&bank, device, &functions, layout, 33, expected_size, expected_size, buffer_at, &live));
  CHECK(!xe_vk_source_bank_init(&bank, device, &functions, layout, 1, expected_size, expected_size - 1, buffer_at, &live));
  CHECK(!creates);
  expected_count = 7; missing = 3;
  CHECK(!xe_vk_source_bank_init(&bank, device, &functions, layout, 7, expected_size, expected_size, buffer_at, &live));
  CHECK(!bank.count && !bank.pool && !creates); missing = UINT32_MAX;
  fail_create = 1;
  CHECK(!xe_vk_source_bank_init(&bank, device, &functions, layout, 7, expected_size, expected_size, buffer_at, &live));
  CHECK(!bank.pool && !bank.count && !live); fail_create = 0;
  fail_allocate = 1;
  CHECK(!xe_vk_source_bank_init(&bank, device, &functions, layout, 7, expected_size, expected_size, buffer_at, &live));
  CHECK(!bank.pool && !bank.count && !live); fail_allocate = 0;
  for (count = 1; count <= XE_VK_SOURCE_BANK_CAPACITY; ++count) {
    expected_count = count;
    CHECK(xe_vk_source_bank_init(&bank, device, &functions, layout, count, expected_size, expected_size, buffer_at, &live));
    CHECK(bank.count == count && live);
    CHECK(!xe_vk_source_bank_init(&bank, device, &functions, layout, count, expected_size, expected_size, buffer_at, &live));
    before_creates = creates; before_allocates = allocates; before_writes = writes;
    for (i = 0; i < 10000; ++i) {
      uint32_t index = i % count;
      CHECK((uintptr_t)xe_vk_source_bank_get(&bank, index, (VkBuffer)(uintptr_t)(100 + index), expected_size) == 1000 + index);
      CHECK(!xe_vk_source_bank_get(&bank, index, (VkBuffer)(uintptr_t)999, expected_size));
      CHECK(!xe_vk_source_bank_get(&bank, count, (VkBuffer)(uintptr_t)100, expected_size));
      CHECK(!xe_vk_source_bank_get(&bank, index, (VkBuffer)(uintptr_t)(100 + index), expected_size - 1));
    }
    CHECK(creates == before_creates && allocates == before_allocates && writes == before_writes);
    xe_vk_source_bank_close(&bank, device, &functions);
    xe_vk_source_bank_close(&bank, device, &functions); CHECK(!live && !bank.count && !bank.pool);
    CHECK(!xe_vk_source_bank_get(&bank, 0, (VkBuffer)(uintptr_t)100, expected_size));
  }
  CHECK(destroys == allocates);
}
static uint32_t random_state = 0x9482abcd;
static uint32_t random_word(void) {
  random_state ^= random_state << 13; random_state ^= random_state >> 17;
  random_state ^= random_state << 5; return random_state;
}
static void view_tests(void) {
  xe_gpu_source_view view, untouched;
  unsigned i;
  uint64_t capacity = UINT64_C(2) << 30;
  uint64_t seen_scales = 0;
  memset(&untouched, 0x5a, sizeof(untouched)); view = untouched;
  CHECK(!xe_gpu_source_view_plan(capacity, 0, 0, capacity, 256, 1, &view));
  CHECK(!memcmp(&view, &untouched, sizeof(view)));
  CHECK(!xe_gpu_source_view_plan(capacity, 1, 16, capacity, 256, 1, &view));
  CHECK(!xe_gpu_source_view_plan(capacity, capacity - 16, 17, capacity, 256, 1, &view));
  CHECK(!xe_gpu_source_view_plan(UINT64_MAX, 0, UINT64_MAX, UINT64_MAX, 256, 0, &view));
  CHECK(!xe_gpu_source_view_plan(capacity, 0, 16, capacity, 0, 1, &view));
  CHECK(!xe_gpu_source_view_plan(capacity, 0, 16, capacity, 3, 1, &view));
  CHECK(xe_gpu_source_view_plan(capacity, capacity - 16, 1, capacity, 256, 1, &view));
  CHECK(view.persistent && !view.descriptor_offset && view.descriptor_range == capacity && view.shader_offset == capacity - 16);
  CHECK(xe_gpu_source_view_plan(UINT64_C(8) << 30, UINT64_C(5) << 30, 4096,
      capacity, 256, 1, &view));
  CHECK(!view.persistent && view.descriptor_offset == (UINT64_C(5) << 30) && !view.shader_offset && view.descriptor_range == 4096);
  CHECK(!xe_gpu_source_view_plan(capacity, 16, 16, capacity, 256, 0, &view));
  CHECK(xe_gpu_source_view_plan(capacity, 16, 16, capacity, 256, 1, &view));
  CHECK(xe_gpu_source_view_plan(UINT64_C(8) << 30, UINT32_MAX - 15, 256, UINT64_C(8) << 30, 16, 1, &view));
  CHECK(!view.persistent && !view.shader_offset); /* uint32 shader addition would wrap. */
  CHECK(!xe_gpu_source_view_plan(capacity, 0, capacity, capacity - 1, 256, 0, &view));
  for (i = 0; i < 1000000; ++i) {
    uint64_t start, length, relative, physical;
    uint32_t local, byte, vector;
    unsigned scale_x = random_word() % 7 + 1, scale_y = random_word() % 7 + 1;
    unsigned area = scale_x * scale_y;
    seen_scales |= UINT64_C(1) << ((scale_x - 1) * 7 + scale_y - 1);
    /* Page-based texture origins stay aligned even for odd scale areas.
     * A sparse window begins at a 1 GiB boundary; descriptor extent is 2 GiB. */
    start = ((uint64_t)(random_word() & 0x1ffff) << 12) * area;
    relative = start & ((UINT64_C(1) << 30) - 1);
    length = (uint64_t)(random_word() % 65536 + 1) * area;
    CHECK(xe_gpu_source_view_plan(capacity, relative, length, capacity, 256, 1, &view));
    local = random_word() % (uint32_t)length;
    physical = start - relative + view.descriptor_offset + view.shader_offset + local;
    CHECK(physical == start + local && view.persistent && view.descriptor_range == capacity);
    /* Every aligned 128-bit load containing a required source byte fits the
     * original padded range and names the identical bytes after rebasing. */
    vector = local & ~UINT32_C(15);
    CHECK((uint64_t)vector + 16 <= ((length + 15) & ~UINT64_C(15)));
    for (byte = 0; byte < 16; ++byte) {
      CHECK(start - relative + view.descriptor_offset + view.shader_offset + vector + byte == start + vector + byte);
    }
    /* Same bytes with a range descriptor on a constrained driver or cold-pool failure. */
    CHECK(xe_gpu_source_view_plan(capacity, relative, length, 128 * 1024 * 1024, 256, 0, &view));
    physical = start - relative + view.descriptor_offset + view.shader_offset + local;
    CHECK(physical == start + local && !view.persistent && view.descriptor_range >= length);
    CHECK(view.descriptor_range < length + 16);
  }
  CHECK(seen_scales == ((UINT64_C(1) << 49) - 1));
}
static void resolve_view_tests(void) {
  xe_gpu_resolve_view view, sentinel;
  const uint64_t capacity = UINT64_C(1) << 29;
  uint64_t base, start, length, limit, alignment, expected_offset, end, padded;
  uint64_t tiled, old_address, rebased_address;
  unsigned i, j, transient = 0, persistent = 0, rejected = 0;
  int available, success, expected;
  memset(&sentinel,0x5a,sizeof(sentinel)); view = sentinel;
#define REJECT(c,b,s,n,m,a,p) do { \
  CHECK(!xe_gpu_resolve_view_plan(c,b,s,n,m,a,p,&view)); \
  CHECK(!memcmp(&view,&sentinel,sizeof(view))); \
} while (0)
  REJECT(capacity,0,0,0,capacity,256,0);
  REJECT(capacity,0,0,16,capacity,0,0);
  REJECT(capacity,0,0,16,capacity,3,0);
  REJECT(capacity,1,16,16,capacity,256,0);
  REJECT(capacity,16,0,16,capacity,256,1);
  REJECT(capacity,0,capacity,1,capacity,256,1);
  REJECT(capacity,0,capacity-16,UINT64_MAX,capacity,256,1);
  REJECT(UINT64_MAX,0,UINT64_MAX-16,16,UINT64_MAX,256,1);
  REJECT(UINT64_C(1)<<33,0,UINT64_C(1)<<32,16,UINT64_MAX,256,1);
  REJECT(capacity-1,0,capacity-16,15,capacity,256,1);
  REJECT(capacity,0,0,16,15,256,0);
#undef REJECT
  CHECK(!xe_gpu_resolve_view_plan(capacity,0,0,16,capacity,256,0,NULL));
  /* Descriptor alignment exceeds base alignment: round DOWN and keep the
   * residual in the shader constant instead of losing the first bytes. */
  CHECK(xe_gpu_resolve_view_plan(capacity,4100,8192,17,128*1024*1024,4096,0,&view));
  CHECK(view.offset==4096 && view.shader_base==4 && view.range==4128);
  CHECK(xe_gpu_resolve_view_plan(capacity,capacity-4,capacity-4,4,capacity,1,1,&view));
  CHECK(view.persistent && !view.offset && view.shader_base==capacity-4);
  CHECK(xe_gpu_resolve_view_plan(UINT64_C(1)<<32,UINT32_MAX-3,UINT32_MAX-3,4,
      UINT64_C(1)<<32,16,0,&view));
  CHECK(view.offset==(UINT64_C(1)<<32)-16 && view.shader_base==12 && view.range==16);
  for (i=0;i<1000000;++i) {
    base = random_word() & UINT32_C(0x1ffffffc);
    start = base + random_word() % (capacity-base);
    length = 1 + random_word() % (capacity-start);
    limit = UINT64_C(1) << (20 + random_word()%10);
    alignment = UINT64_C(1) << (random_word()%17);
    available = (int)(random_word()&1);
    end = start+length; padded = (end+15)&~UINT64_C(15);
    /* Independent CPU reference uses division to round the view origin. */
    expected_offset = base / (alignment<16 ? 16 : alignment) *
        (alignment<16 ? 16 : alignment);
    expected = available && limit>=capacity;
    if (!expected) expected = padded-expected_offset<=limit;
    view=sentinel;
    success=xe_gpu_resolve_view_plan(capacity,base,start,length,limit,alignment,available,&view);
    CHECK(success==expected);
    if (!success) { CHECK(!memcmp(&view,&sentinel,sizeof(view))); ++rejected; continue; }
    CHECK(view.offset%alignment==0 && view.offset%16==0 && view.range<=limit);
    CHECK(view.offset+view.range<=capacity && view.offset<=base);
    CHECK(view.offset+view.shader_base==base);
    CHECK(view.persistent==(available && limit>=capacity));
    if(view.persistent) { CHECK(!view.offset && view.range==capacity); ++persistent; }
    else { CHECK(view.offset==expected_offset && view.offset+view.range==padded); ++transient; }
    /* First/last/arbitrary bytes cover the same interval after native tiling
     * and adding the rebased constant; vector-aligned enclosing stores fit. */
    for(j=0;j<3;++j) {
      old_address = j==0 ? start : j==1 ? end-1 : start+random_word()%length;
      tiled=old_address-base;
      CHECK(tiled<=UINT32_MAX-view.shader_base);
      rebased_address=(uint32_t)tiled+view.shader_base;
      CHECK(view.offset+rebased_address==old_address && rebased_address<view.range);
      CHECK((rebased_address&~UINT64_C(15))+16<=view.range);
    }
  }
  CHECK(transient && persistent && rejected);
  printf("resolve views: 1000000 intervals, %u range / %u persistent / %u fallback; bounds, alignment, failure publication PASS\n",
      transient,persistent,rejected);
}
int main(void) {
  device = (VkDevice)(uintptr_t)3; layout = (VkDescriptorSetLayout)(uintptr_t)5;
  bank_tests(); view_tests(); resolve_view_tests();
  printf("source views: 1000000 address pairs, 320000 immutable lookups, cold failure/reinit/double-close PASS; bank %lu bytes\n",
      (unsigned long)sizeof(xe_vk_source_bank));
  return 0;
}
