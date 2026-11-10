/* One raw interpreter PSO, mutable instruction data, exact RTZ/FTZ oracles. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "libretro/core_alu_spv.h"
#include "libretro/core_float_rtz.h"
#include "vulkan_test_context.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"ALU GPU line %u\n",(unsigned)__LINE__);goto done;}}while(0)
enum {RECORDS=4099,OUTPUT_RECORDS=4105};
static struct buffer input_buffer,output_buffer;
static uint32_t oracle_add(uint32_t a,uint32_t b){uint32_t aa=a&0x7fffffffu,bb=b&0x7fffffffu;
 if(aa>0x7f800000u || bb>0x7f800000u || (aa==0x7f800000u && bb==aa && ((a^b)>>31)))return 0x7fc00000u;
 if(aa==0x7f800000u)return a;
 if(bb==0x7f800000u)return b;
 return xe_float_add_rtz_bits(a,b,0);}
static uint32_t oracle_mul(uint32_t a,uint32_t b){uint32_t aa=a&0x7fffffffu,bb=b&0x7fffffffu;
 if(aa<0x800000u || bb<0x800000u)return 0;
 if(aa>0x7f800000u || bb>0x7f800000u)return 0x7fc00000u;
 if(aa==0x7f800000u || bb==0x7f800000u)return ((a^b)&0x80000000u)|0x7f800000u;
 return xe_float_mul_rtz_bits(a,b,0);}
static uint32_t operand(const uint32_t *record,unsigned source,unsigned lane) {
 unsigned component=((((record[1]>>(16-source*8))&255)>>(lane*2))+lane)&3;
 unsigned is_temp=(record[2]>>(31-source))&1;
 unsigned absolute=is_temp?(record[2]>>(23-source*8))&1:(record[0]>>7)&1;
 unsigned negate=(record[1]>>(26-source))&1;
 uint32_t bits=record[3+source*4+component];
 return (bits&~((uint32_t)absolute<<31))^((uint32_t)negate<<31);
}
static uint32_t next(uint32_t *seed){*seed=*seed*1664525u+1013904223u;return *seed;}
static int same(uint32_t a,uint32_t b){return a==b || ((a&0x7fffffffu)>0x7f800000u && (b&0x7fffffffu)>0x7f800000u);}
int main(void){
#ifdef _WIN32
 HMODULE loader=LoadLibraryA("vulkan-1.dll");FARPROC entry;
#else
 void *loader=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL),*entry;
#endif
 static VkPhysicalDevice physical[8];static VkQueueFamilyProperties families[32];
 static VkPhysicalDeviceProperties properties;
 VkApplicationInfo app={0};VkInstanceCreateInfo instance={0};VkDeviceQueueCreateInfo queue_info={0};VkDeviceCreateInfo device_info={0};
 VkQueue queue=VK_NULL_HANDLE;VkDescriptorSetLayoutBinding bindings[2];VkDescriptorSetLayoutCreateInfo set_info={0};
 VkDescriptorSetLayout set_layout=VK_NULL_HANDLE;VkPipelineLayoutCreateInfo layout_info={0};VkPipelineLayout layout=VK_NULL_HANDLE;
 VkShaderModuleCreateInfo module_info={0};VkShaderModule module=VK_NULL_HANDLE;VkComputePipelineCreateInfo pipeline_info={0};VkPipeline pipeline=VK_NULL_HANDLE;
 VkDescriptorPoolSize pool_size={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2};VkDescriptorPoolCreateInfo pool_info={0};VkDescriptorPool pool=VK_NULL_HANDLE;
 VkDescriptorSetAllocateInfo set_allocate={0};VkDescriptorSet set=VK_NULL_HANDLE;VkDescriptorBufferInfo infos[2];VkWriteDescriptorSet writes[2];
 VkCommandPoolCreateInfo command_pool_info={0};VkCommandPool command_pool=VK_NULL_HANDLE;VkCommandBufferAllocateInfo command_allocate={0};
 VkCommandBuffer command;VkCommandBufferBeginInfo begin={0};VkMemoryBarrier barrier={0};VkFenceCreateInfo fence_info={0};VkFence fence=VK_NULL_HANDLE;VkSubmitInfo submit={0};
 xe_spv_binary code={0};uint32_t count,family,i,lane,round,limit,opcode,*in,*out,a,b,c,wanted,seed=0x81732019u;
 unsigned modules=0,pipelines=0,cases=0;float priority=1.0f;int result=1;
 static const uint32_t edges[]={0,0x80000000u,1,0x80000001u,0x7fffffu,0x807fffffu,0x800000u,0x80800000u,
  0x3f800000u,0xbf800000u,0x3f000000u,0xbf000000u,0x7f7fffffu,0xff7fffffu,0x7f800000u,0xff800000u,0x7fc12345u,0xff812345u};
 (void)shader;CHECK(loader && xe_alu_spv_build(&code));
#ifdef _WIN32
 entry=GetProcAddress(loader,"vkGetInstanceProcAddr");
#else
 entry=dlsym(loader,"vkGetInstanceProcAddr");
#endif
 memcpy(&g.get,&entry,sizeof(g.get));CHECK(g.get);LOAD(vkCreateInstance);
 app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;app.apiVersion=VK_API_VERSION_1_0;
 instance.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;instance.pApplicationInfo=&app;OK(g.vkCreateInstance(&instance,NULL,&g.instance));
 LOAD(vkDestroyInstance);LOAD(vkEnumeratePhysicalDevices);LOAD(vkGetPhysicalDeviceProperties);LOAD(vkGetPhysicalDeviceMemoryProperties);LOAD(vkGetPhysicalDeviceQueueFamilyProperties);LOAD(vkCreateDevice);
 count=8;OK(g.vkEnumeratePhysicalDevices(g.instance,&count,physical));CHECK(count);g.vkGetPhysicalDeviceProperties(physical[0],&properties);
 fprintf(stderr,"ALU_DEVICE vendor=%u device=%u driver=%u name=%s\n",properties.vendorID,properties.deviceID,properties.driverVersion,properties.deviceName);
 g.vkGetPhysicalDeviceMemoryProperties(physical[0],&g.memory);count=32;g.vkGetPhysicalDeviceQueueFamilyProperties(physical[0],&count,families);
 for(family=0;family<count;++family){if(families[family].queueFlags&VK_QUEUE_COMPUTE_BIT)break;}
 CHECK(family<count);
 queue_info.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;queue_info.queueFamilyIndex=family;queue_info.queueCount=1;queue_info.pQueuePriorities=&priority;
 device_info.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;device_info.queueCreateInfoCount=1;device_info.pQueueCreateInfos=&queue_info;OK(g.vkCreateDevice(physical[0],&device_info,NULL,&g.device));
 LOAD(vkDestroyDevice);LOAD(vkGetDeviceQueue);LOAD(vkCreateBuffer);LOAD(vkDestroyBuffer);LOAD(vkGetBufferMemoryRequirements);LOAD(vkAllocateMemory);LOAD(vkFreeMemory);
 LOAD(vkBindBufferMemory);LOAD(vkMapMemory);LOAD(vkUnmapMemory);LOAD(vkFlushMappedMemoryRanges);LOAD(vkInvalidateMappedMemoryRanges);
 LOAD(vkCreateDescriptorSetLayout);LOAD(vkDestroyDescriptorSetLayout);LOAD(vkCreatePipelineLayout);LOAD(vkDestroyPipelineLayout);LOAD(vkCreateShaderModule);LOAD(vkDestroyShaderModule);
 LOAD(vkCreateComputePipelines);LOAD(vkDestroyPipeline);LOAD(vkCreateDescriptorPool);LOAD(vkDestroyDescriptorPool);LOAD(vkAllocateDescriptorSets);LOAD(vkUpdateDescriptorSets);
 LOAD(vkCreateCommandPool);LOAD(vkDestroyCommandPool);LOAD(vkAllocateCommandBuffers);LOAD(vkResetCommandPool);LOAD(vkBeginCommandBuffer);LOAD(vkEndCommandBuffer);
 LOAD(vkCmdPipelineBarrier);LOAD(vkCmdBindPipeline);LOAD(vkCmdBindDescriptorSets);LOAD(vkCmdDispatch);LOAD(vkCreateFence);LOAD(vkDestroyFence);LOAD(vkResetFences);LOAD(vkQueueSubmit);LOAD(vkWaitForFences);
 g.vkGetDeviceQueue(g.device,family,0,&queue);CHECK(make_buffer(&input_buffer,RECORDS*64) && make_buffer(&output_buffer,OUTPUT_RECORDS*32));
 memset(bindings,0,sizeof(bindings));for(i=0;i<2;++i){bindings[i].binding=i;bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}
 set_info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;set_info.bindingCount=2;set_info.pBindings=bindings;OK(g.vkCreateDescriptorSetLayout(g.device,&set_info,NULL,&set_layout));
 layout_info.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;layout_info.setLayoutCount=1;layout_info.pSetLayouts=&set_layout;OK(g.vkCreatePipelineLayout(g.device,&layout_info,NULL,&layout));
 module_info.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;module_info.codeSize=code.count*4;module_info.pCode=code.words;++modules;OK(g.vkCreateShaderModule(g.device,&module_info,NULL,&module));
 pipeline_info.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;pipeline_info.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
 pipeline_info.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pipeline_info.stage.module=module;pipeline_info.stage.pName="main";pipeline_info.layout=layout;
 ++pipelines;OK(g.vkCreateComputePipelines(g.device,VK_NULL_HANDLE,1,&pipeline_info,NULL,&pipeline));xe_spv_binary_close(&code);
 pool_info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pool_info.maxSets=1;pool_info.poolSizeCount=1;pool_info.pPoolSizes=&pool_size;OK(g.vkCreateDescriptorPool(g.device,&pool_info,NULL,&pool));
 set_allocate.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;set_allocate.descriptorPool=pool;set_allocate.descriptorSetCount=1;set_allocate.pSetLayouts=&set_layout;OK(g.vkAllocateDescriptorSets(g.device,&set_allocate,&set));
 command_pool_info.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;command_pool_info.queueFamilyIndex=family;OK(g.vkCreateCommandPool(g.device,&command_pool_info,NULL,&command_pool));
 command_allocate.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;command_allocate.commandPool=command_pool;command_allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;command_allocate.commandBufferCount=1;OK(g.vkAllocateCommandBuffers(g.device,&command_allocate,&command));
 fence_info.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;OK(g.vkCreateFence(g.device,&fence_info,NULL,&fence));
 in=(uint32_t*)input_buffer.mapped;out=(uint32_t*)output_buffer.mapped;
 for(round=0;round<3;++round){limit=round==1?RECORDS-17:RECORDS;
  for(i=0;i<RECORDS;++i){opcode=i%4==0?0:i%4==1?1:i%4==2?11:(i/4)%32;
   in[i*16]=next(&seed);in[i*16+1]=next(&seed);in[i*16+2]=(next(&seed)&0xe0ffffffu)|(opcode<<24);
   for(lane=0;lane<12;++lane)in[i*16+3+lane]=i<1296?edges[(i/(lane<4?1:lane<8?18:324)+lane)%18]:next(&seed);
   in[i*16+15]=0xa311900du;}
  memset(out,0x6d,OUTPUT_RECORDS*32);CHECK(cache_buffer(&input_buffer,0) && cache_buffer(&output_buffer,0));
  memset(infos,0,sizeof(infos));infos[0].buffer=input_buffer.buffer;infos[0].range=limit*64;infos[1].buffer=output_buffer.buffer;infos[1].range=OUTPUT_RECORDS*32;
  memset(writes,0,sizeof(writes));for(i=0;i<2;++i){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=set;writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=&infos[i];}
  g.vkUpdateDescriptorSets(g.device,2,writes,0,NULL);OK(g.vkResetCommandPool(g.device,command_pool,0));
  begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;OK(g.vkBeginCommandBuffer(command,&begin));
  barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
  g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,NULL,0,NULL);
  g.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);g.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);g.vkCmdDispatch(command,(OUTPUT_RECORDS+63)/64+1,1,1);
  barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
  OK(g.vkEndCommandBuffer(command));OK(g.vkResetFences(g.device,1,&fence));submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&command;
  OK(g.vkQueueSubmit(queue,1,&submit,fence));OK(g.vkWaitForFences(g.device,1,&fence,VK_TRUE,UINT64_C(5000000000)));CHECK(cache_buffer(&output_buffer,1));
  for(i=0;i<OUTPUT_RECORDS;++i){opcode=i<limit?(in[i*16+2]>>24)&31:31;
   if(i>=limit){for(lane=0;lane<8;++lane)CHECK(out[i*8+lane]==0x6d6d6d6du);continue;}
   CHECK(out[i*8+4]==(uint32_t)(opcode==0 || opcode==1 || opcode==11));
   for(lane=0;lane<4;++lane){a=operand(in+i*16,0,lane);b=operand(in+i*16,1,lane);c=operand(in+i*16,2,lane);
    wanted=opcode==0?oracle_add(a,b):opcode==1?oracle_mul(a,b):opcode==11?oracle_add(oracle_mul(a,b),c):0x6d6d6d6du;
    if(!same(out[i*8+lane],wanted)){fprintf(stderr,"round %u case %u lane %u op %u: %08x != %08x (a %08x b %08x c %08x)\n",round,i,lane,opcode,out[i*8+lane],wanted,a,b,c);goto done;}}
   for(lane=5;lane<8;++lane){CHECK(out[i*8+lane]==0x6d6d6d6du);}
   ++cases;}
 }
 CHECK(modules==1 && pipelines==1);printf("PASS: %u dynamic ALU records, raw source swizzles/absolute/negation, exact finite RTZ/FTZ and nonfinite classes, unsupported/bounds guards; one module and PSO across three data replays\n",cases);result=0;
done:
 if(g.device){if(fence)g.vkDestroyFence(g.device,fence,NULL);
  if(command_pool)g.vkDestroyCommandPool(g.device,command_pool,NULL);
  if(pool)g.vkDestroyDescriptorPool(g.device,pool,NULL);
  if(pipeline)g.vkDestroyPipeline(g.device,pipeline,NULL);
  if(module)g.vkDestroyShaderModule(g.device,module,NULL);
  if(layout)g.vkDestroyPipelineLayout(g.device,layout,NULL);
  if(set_layout)g.vkDestroyDescriptorSetLayout(g.device,set_layout,NULL);
  close_buffer(&output_buffer);close_buffer(&input_buffer);g.vkDestroyDevice(g.device,NULL);}
 if(g.instance)g.vkDestroyInstance(g.instance,NULL);
 xe_spv_binary_close(&code);
#ifdef _WIN32
 if(loader)FreeLibrary(loader);
#else
 if(loader)dlclose(loader);
#endif
 return result;}
