/* Dynamic boundary, independent owners, packet guards and failure cleanup. */
#define VK_NO_PROTOTYPES
#include "transfer_uint_revision_load.h"
#include <stdio.h>
#include <stdlib.h>
static unsigned char live[2048];
static unsigned next_id=1,calls,fail_at;static int error;
#define HANDLE(type,id) ((type)(uintptr_t)(id))
#define CHECK(x) do {if(!(x)){fprintf(stderr,"Revision ABI line%u\n",(unsigned)__LINE__);return 1;}}while(0)
static uintptr_t make(unsigned kind) {
 unsigned id=next_id++;if(id>=sizeof(live)){error=1;return 0;}live[id]=(unsigned char)kind;return id;
}
static void release(uintptr_t id,unsigned kind) {
 if(!id || id>=sizeof(live) || live[id]!=kind){error=1;return;}live[id]=0;
}
static int empty(void) {
 unsigned i;if(error)return 0;for(i=1;i<next_id;++i)if(live[i])return 0;return 1;
}
static VKAPI_ATTR VkResult VKAPI_CALL shader(VkDevice device,const VkShaderModuleCreateInfo *info,const VkAllocationCallbacks *allocator,VkShaderModule *out) {
 (void)device;(void)info;(void)allocator;
 if(++calls==fail_at){*out=HANDLE(VkShaderModule,0xdead);return VK_ERROR_OUT_OF_HOST_MEMORY;}
 *out=HANDLE(VkShaderModule,make(1));return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL close_shader(VkDevice device,VkShaderModule value,const VkAllocationCallbacks *allocator) {
 (void)device;(void)allocator;release((uintptr_t)value,1);
}
static VKAPI_ATTR VkResult VKAPI_CALL layout(VkDevice device,const VkPipelineLayoutCreateInfo *info,const VkAllocationCallbacks *allocator,VkPipelineLayout *out) {
 (void)device;(void)info;(void)allocator;
 if(++calls==fail_at){*out=HANDLE(VkPipelineLayout,0xdead);return VK_ERROR_OUT_OF_HOST_MEMORY;}
 *out=HANDLE(VkPipelineLayout,make(2));return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL close_layout(VkDevice device,VkPipelineLayout value,const VkAllocationCallbacks *allocator) {
 (void)device;(void)allocator;release((uintptr_t)value,2);
}
static VKAPI_ATTR VkResult VKAPI_CALL pipelines(VkDevice device,VkPipelineCache cache,uint32_t count,const VkGraphicsPipelineCreateInfo *info,const VkAllocationCallbacks *allocator,VkPipeline *out) {
 unsigned i;int failed=++calls==fail_at;(void)device;(void)cache;(void)info;(void)allocator;
 for(i=0;i<count;++i)out[i]=failed && i>=2?VK_NULL_HANDLE:HANDLE(VkPipeline,make(3));
 return failed?VK_ERROR_OUT_OF_HOST_MEMORY:VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL close_pipeline(VkDevice device,VkPipeline value,const VkAllocationCallbacks *allocator) {
 (void)device;(void)allocator;release((uintptr_t)value,3);
}
int main(int argc,char **argv) {
 static const unsigned failures[]={1,2,11,151};
 xe_test_transfer_revision_library library={0};xe_test_transfer_revision_entry entry=NULL;
 xe_test_transfer_revision_config config={0};xe_test_transfer_revision_key key={0};
 unsigned f,m,i;unsigned char *packet;void *a,*b;VkPipeline pa,pb;
#ifdef _WIN32
 FARPROC symbol;
#else
 void *symbol;
#endif
 CHECK(argc==2 && xe_test_transfer_revision_load(&library,argv[1]));
#ifdef _WIN32
 symbol=GetProcAddress(library.module,"xe_test_transfer_revision_get");
#else
 symbol=dlsym(library.module,"xe_test_transfer_revision_get");
#endif
 memcpy(&entry,&symbol,sizeof(entry));CHECK(entry && !entry(0,sizeof(*library.api)) && !entry(1,0));
 CHECK(!library.api->open(NULL) && !library.api->layout(NULL) && !library.api->pipeline(NULL,4,0,0));
 CHECK(library.api->close(NULL));
 config.device=HANDLE(VkDevice,1);config.images=HANDLE(VkDescriptorSetLayout,2);config.vertex=HANDLE(VkShaderModule,3);
 config.native2=config.sample_shading=1;config.scale_x=3;config.scale_y=5;
 config.create_shader=shader;config.destroy_shader=close_shader;config.create_layout=layout;
 config.destroy_layout=close_layout;config.create_pipelines=pipelines;config.destroy_pipeline=close_pipeline;
 for(f=0;f<4;++f)for(m=0;m<3;++m)config.passes[f][m]=HANDLE(VkRenderPass,1000+f*3+m);
 for(i=0;i<sizeof(failures)/sizeof(*failures);++i) {
  calls=0;next_id=1;fail_at=failures[i];
  CHECK(!library.api->open(&config) && empty());
 }
 fail_at=0;calls=0;next_id=1;
 a=library.api->open(&config);b=library.api->open(&config);CHECK(a && b && a!=b);
 pa=library.api->pipeline(a,4,0,0);pb=library.api->pipeline(b,4,0,0);CHECK(pa && pb && pa!=pb);
 CHECK(library.api->close(a) && live[(uintptr_t)pb]==3 && library.api->layout(b));
 CHECK(library.api->close(b) && empty());
 packet=(unsigned char*)malloc(XE_TEST_TRANSFER_PACKET_BYTES+16);CHECK(packet);
 memset(packet,0xa5,XE_TEST_TRANSFER_PACKET_BYTES+16);
 key.dest_scale_x=key.dest_scale_y=key.source_scale_x=key.source_scale_y=1;
 key.source_pitch=33;key.dest_pitch=32;key.delta=-7;key.source_format=4;key.dest_format=6;
 CHECK(!library.api->packet(&key,packet,XE_TEST_TRANSFER_PACKET_BYTES-1));
 for(i=0;i<XE_TEST_TRANSFER_PACKET_BYTES+16;++i)CHECK(packet[i]==0xa5);
 CHECK(library.api->packet(&key,packet,XE_TEST_TRANSFER_PACKET_BYTES));
 for(i=XE_TEST_TRANSFER_PACKET_BYTES;i<XE_TEST_TRANSFER_PACKET_BYTES+16;++i)CHECK(packet[i]==0xa5);
 memset(packet,0xa5,XE_TEST_TRANSFER_PACKET_BYTES+16);key.source_wide=1;
 CHECK(!library.api->packet(&key,packet,XE_TEST_TRANSFER_PACKET_BYTES));
 for(i=0;i<XE_TEST_TRANSFER_PACKET_BYTES+16;++i)CHECK(packet[i]==0xa5);
 free(packet);CHECK(xe_test_transfer_revision_unload(&library));
 CHECK(!library.api && !library.module);
 puts("PASS: revision ABI negotiation, independent owners, poisoned/partial failures and packet guards");return 0;
}
