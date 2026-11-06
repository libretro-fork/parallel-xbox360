/* C89 final-record storage, no-copy growth, real Vulkan C fields. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned allocations,releases;
static int fail_alloc;
static void *allocate(size_t n) {
  void *p;
  ++allocations; if(fail_alloc) return NULL;
  p=malloc(n); if(p) memset(p,0xa7,n); return p;
}
static void release(void *p) { if(p) ++releases; free(p); }
#define XE_DESCRIPTOR_ALLOC allocate
#define XE_DESCRIPTOR_FREE release
#include "core_descriptor_scratch.h"
#include "core_vk_descriptor_image.h"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"descriptor line %d: %s\n",__LINE__,#x); return 1; } } while(0)
#ifdef XE_DESCRIPTOR_BRIDGE
extern int xe_test_descriptor_reference(void);
#endif
int main(void) {
  xe_descriptor_scratch storage={NULL,0};
  xe_descriptor_plan plan,saved;
  size_t counts[4]={3,2,4,1},i;
  uintptr_t old;
  unsigned before;
  VkDescriptorImageInfo info;
  VkImageView image=(VkImageView)((uint64_t)1<<48);
  VkSampler sampler=(VkSampler)(((uint64_t)1<<48)+7);
  CHECK(xe_descriptor_prepare(&storage,counts,sizeof(info),&plan));
  CHECK(plan.count==10 && plan.bytes==10*sizeof(info) && plan.offsets[0]==0 &&
      plan.offsets[1]==3 && plan.offsets[2]==5 && plan.offsets[3]==9);
  for(i=0;i<plan.bytes;++i) CHECK(storage.data[i]==0xa7);
  memset(storage.data,0x42,plan.bytes); old=(uintptr_t)storage.data; saved=plan;
  counts[0]=100; fail_alloc=1;
  CHECK(!xe_descriptor_prepare(&storage,counts,sizeof(info),&plan));
  CHECK((uintptr_t)storage.data==old && !memcmp(&plan,&saved,sizeof(plan)) && !releases);
  for(i=0;i<saved.bytes;++i) CHECK(storage.data[i]==0x42);
  fail_alloc=0;
  CHECK(xe_descriptor_prepare(&storage,counts,sizeof(info),&plan));
  CHECK((uintptr_t)storage.data!=old && releases==1);
  for(i=0;i<plan.bytes;++i) CHECK(storage.data[i]==0xa7); /* old bytes not copied */
  saved=plan; before=allocations;
  counts[0]=(size_t)-1; counts[1]=1;
  CHECK(!xe_descriptor_prepare(&storage,counts,sizeof(info),&plan));
  counts[0]=(size_t)-1/sizeof(info)+1; counts[1]=counts[2]=counts[3]=0;
  CHECK(!xe_descriptor_prepare(&storage,counts,sizeof(info),&plan));
  CHECK(!xe_descriptor_prepare(&storage,counts,0,&plan));
  CHECK(allocations==before && !memcmp(&plan,&saved,sizeof(plan)));
  for(i=0;i<10000;++i) {
    counts[0]=i%20; counts[1]=i%17; counts[2]=i%7; counts[3]=i%5;
    CHECK(xe_descriptor_prepare(&storage,counts,sizeof(info),&plan));
  }
  CHECK(allocations==before);
  memset(counts,0,sizeof(counts));
  CHECK(xe_descriptor_prepare(&storage,counts,sizeof(info),&plan) && !plan.count && !plan.bytes);
  memset(&info,0xa7,sizeof(info)); xe_vk_descriptor_texture(&info,image);
  CHECK(info.sampler==VK_NULL_HANDLE && info.imageView==image &&
      info.imageLayout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  memset(&info,0xa7,sizeof(info)); xe_vk_descriptor_sampler(&info,sampler);
  CHECK(info.sampler==sampler && info.imageView==VK_NULL_HANDLE && info.imageLayout==VK_IMAGE_LAYOUT_UNDEFINED);
  xe_descriptor_scratch_free(&storage); xe_descriptor_scratch_free(&storage);
  CHECK(!storage.data && !storage.capacity && releases==2);
#ifdef XE_DESCRIPTOR_BRIDGE
  CHECK(xe_test_descriptor_reference());
#endif
  puts("Descriptor scratch: checked spans, no-copy growth, complete Vulkan fields and failed allocation pass");
  return 0;
}
