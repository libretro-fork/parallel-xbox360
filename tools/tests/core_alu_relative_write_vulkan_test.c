/* Additional continuation coverage. Original corpus remains separately intact.
 * Borrow mapped input/output spans; one final fence, no per-pass readback. C89. */
#define main xe_relative_original_continuation_fixture
#include "core_alu_resume_vulkan_test.c"
#undef main
#ifndef XE_ALU_RESUME_RELATIVE_TEMP_WRITE
#error Relative-write capability is required for this fixture
#endif
enum {RELATIVE_CASES=48,RELATIVE_RECORDS=96,RELATIVE_OUTPUTS=98,RELATIVE_INPUT_BYTES=(1+RELATIVE_RECORDS*16+RELATIVE_RECORDS*256)*4};
static unsigned relative_status(unsigned which)
{return which==38 || which==39 || which==42?3u:1u;}
static unsigned relative_target(unsigned which)
{
 if(which==34 || which==35)return 63;
 if(which==36 || which==37 || which==40 || which==41 || which==47)return 0;
 if(which==43 || which==44)return 2;
 return 3;
}
static void relative_program(uint32_t *p,unsigned which)
{
 unsigned vm=0,sm=0,vd=2,sd=2,vr=0,sr=0,slot=4;uint32_t slots=0;
 memset(p,0,84);
 if(which<16){vm=which;vr=1;}
 else if(which<32){sm=which-16;sr=1;vd=63;vr=1;}
 else if(which==32){vm=5;sm=10;vr=sr=1;}
 else if(which==33){vm=sm=15;vr=sr=1;}
 else if(which==34){vm=15;vd=63;vr=1;}
 else if(which==35){sm=15;sd=0;sr=1;}
 else if(which==36){vm=15;vd=1;vr=1;}
 else if(which==37){sm=15;sd=1;sr=1;}
 else if(which==38){vm=15;vd=1;vr=1;}
 else if(which==39){sm=15;sd=63;sr=1;}
 else if(which==40 || which==41 || which==47){vd=sd=63;vr=sr=1;if(which==41)vm=sm=15;}
 else if(which==42){vm=15;vd=0;vr=1;}
 else {vd=sd=0;vr=sr=1;if(which&1)vm=15;else sm=15;slot=5;}
 if(which>=43 && which<=46){
  control_cf(p,0,5,0x7000);control_cf(p,1,4|(1u<<16)|(which>=45?1u<<13:0),0x7000);
  control_cf(p,2,slot|(1u<<12),0x1000);control_cf(p,3,2|(1u<<16),0x8000);
  control_cf(p,4,1,0x8000);control_cf(p,5,6|(1u<<12),0x2000);
 }else{
  control_cf(p,0,3,0x7000);control_cf(p,1,slot|(1u<<12),0x1000);
  control_cf(p,2,1,0x8000);control_cf(p,3,6|(1u<<12),0x2000);
 }
 alu(p+slot*3,vd|(vr<<6)|(sd<<8)|(sr<<14),vm,sm,sm?0u:50u,0,0,1,0,0);
 if(which==41)p[slot*3+1]|=(1u<<28)|(1u<<27);
 control_alu(p,6,62,relative_target(which),3,15,1,&slots);
}
static void relative_context(uint32_t *context,unsigned which)
{
 unsigned start=1,count=1;int step=0;
 memset(context,0,160);
 if(which==34 || which==36 || which==37)start=0;
 if(which==35 || which==38 || which==42)start=63;
 if(which==36 || which==37){count=2;step=-1;}
 if(which==42){count=2;step=1;}
 if(which>=43 && which<=46){
  count=2;step=1;context[9]=loop_constant(2,which>=45?99u:3u,which>=45?1:-1);
 }
 if(which==47)start=255;
 context[8]=loop_constant(count,start,step);
}
static uint32_t relative_golden(unsigned which,unsigned lane)
{
 unsigned mask;float f;uint32_t bits;
 f=(float)(relative_target(which)+1);memcpy(&bits,&f,4);
 if(which<16){mask=which;if(mask&(1u<<lane))bits=0x40400000u;}
 else if(which<32){mask=which-16;if(mask&(1u<<lane))bits=0x40000000u;}
 else if(which==32)bits=lane&1?0x40000000u:0x40400000u;
 else if(which==33 || which==35 || which==37 || which==44 || which==46)bits=0x40000000u;
 else if(which==34 || which==36 || which==43 || which==45)bits=0x40400000u;
 return bits;
}
int main(void)
{
#ifdef _WIN32
 HMODULE loader=LoadLibraryA("vulkan-1.dll");FARPROC entry;
#else
 void *loader=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL),*entry;
#endif
 VkPhysicalDevice physical[8];VkQueueFamilyProperties families[32];VkPhysicalDeviceProperties properties;
 VkApplicationInfo app={0};VkInstanceCreateInfo instance={0};VkDeviceQueueCreateInfo queue_info={0};VkDeviceCreateInfo device_info={0};
 VkQueue queue=VK_NULL_HANDLE;VkDescriptorSetLayoutBinding bindings[6];VkDescriptorSetLayoutCreateInfo set_info={0};
 VkDescriptorSetLayout set_layout=VK_NULL_HANDLE;VkPipelineLayoutCreateInfo layout_info={0};VkPipelineLayout layout=VK_NULL_HANDLE;
 VkShaderModuleCreateInfo module_info={0};VkShaderModule module=VK_NULL_HANDLE;VkComputePipelineCreateInfo pipeline_info={0};VkPipeline pipeline=VK_NULL_HANDLE;
 VkDescriptorPoolSize pool_size[2]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,5},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1}};
 VkDescriptorPoolCreateInfo pool_info={0};VkDescriptorPool pool=VK_NULL_HANDLE;VkDescriptorSetAllocateInfo set_allocate={0};VkDescriptorSet set=VK_NULL_HANDLE;
 VkDescriptorBufferInfo infos[6];VkWriteDescriptorSet writes[6];
 VkCommandPoolCreateInfo command_pool_info={0};VkCommandPool command_pool=VK_NULL_HANDLE;VkCommandBufferAllocateInfo command_allocate={0};
 VkCommandBuffer command;VkCommandBufferBeginInfo begin={0};VkMemoryBarrier barrier={0};VkFenceCreateInfo fence_info={0};VkFence fence=VK_NULL_HANDLE;VkSubmitInfo submit={0};
 xe_spv_binary code={0};uint32_t count=8,family,i,lane,pass,*in,*out,*programs,*constants,*context,*state,*descriptor,*temporary;
 uint32_t native_code[21],reference[7],bank[40];unsigned which,status,budget,native=0,literals=0;float priority=1.0f;int result=1,submitted=0;
 const char *validation=getenv("XE_RELATIVE_GPU_VALIDATION");const char *layer="VK_LAYER_KHRONOS_validation";
 (void)reference;
 CHECK(loader && xe_alu_control_resume_spv_build(&code));CHECK(capture_module(&code));
#ifdef _WIN32
 entry=GetProcAddress(loader,"vkGetInstanceProcAddr");
#else
 entry=dlsym(loader,"vkGetInstanceProcAddr");
#endif
 memcpy(&g.get,&entry,sizeof(g.get));CHECK(g.get);LOAD(vkCreateInstance);
 app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;app.apiVersion=VK_API_VERSION_1_0;
 instance.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;instance.pApplicationInfo=&app;
 if(validation && !strcmp(validation,"1")){instance.enabledLayerCount=1;instance.ppEnabledLayerNames=&layer;}
 OK(g.vkCreateInstance(&instance,NULL,&g.instance));
 LOAD(vkDestroyInstance);LOAD(vkEnumeratePhysicalDevices);LOAD(vkGetPhysicalDeviceProperties);LOAD(vkGetPhysicalDeviceMemoryProperties);LOAD(vkGetPhysicalDeviceQueueFamilyProperties);LOAD(vkCreateDevice);
 OK(g.vkEnumeratePhysicalDevices(g.instance,&count,physical));CHECK(count);g.vkGetPhysicalDeviceProperties(physical[0],&properties);
 CHECK(properties.limits.maxPerStageDescriptorStorageBuffers>=5 && properties.limits.maxDescriptorSetStorageBuffers>=5);
 fprintf(stderr,"RELATIVE_WRITE_DEVICE vendor=%u device=%u driver=%u name=%s validation=%u\n",properties.vendorID,properties.deviceID,properties.driverVersion,properties.deviceName,instance.enabledLayerCount);
 g.vkGetPhysicalDeviceMemoryProperties(physical[0],&g.memory);count=32;g.vkGetPhysicalDeviceQueueFamilyProperties(physical[0],&count,families);
 for(family=0;family<count;++family)if(families[family].queueFlags&VK_QUEUE_COMPUTE_BIT)break;
 CHECK(family<count);queue_info.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;queue_info.queueFamilyIndex=family;queue_info.queueCount=1;queue_info.pQueuePriorities=&priority;
 device_info.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;device_info.queueCreateInfoCount=1;device_info.pQueueCreateInfos=&queue_info;OK(g.vkCreateDevice(physical[0],&device_info,NULL,&g.device));
 LOAD(vkDestroyDevice);LOAD(vkGetDeviceQueue);LOAD(vkCreateBuffer);LOAD(vkDestroyBuffer);LOAD(vkGetBufferMemoryRequirements);LOAD(vkAllocateMemory);LOAD(vkFreeMemory);
 LOAD(vkBindBufferMemory);LOAD(vkMapMemory);LOAD(vkUnmapMemory);LOAD(vkFlushMappedMemoryRanges);LOAD(vkInvalidateMappedMemoryRanges);
 LOAD(vkCreateDescriptorSetLayout);LOAD(vkDestroyDescriptorSetLayout);LOAD(vkCreatePipelineLayout);LOAD(vkDestroyPipelineLayout);LOAD(vkCreateShaderModule);LOAD(vkDestroyShaderModule);
 LOAD(vkCreateComputePipelines);LOAD(vkDestroyPipeline);LOAD(vkCreateDescriptorPool);LOAD(vkDestroyDescriptorPool);LOAD(vkAllocateDescriptorSets);LOAD(vkUpdateDescriptorSets);
 LOAD(vkCreateCommandPool);LOAD(vkDestroyCommandPool);LOAD(vkAllocateCommandBuffers);LOAD(vkBeginCommandBuffer);LOAD(vkEndCommandBuffer);
 LOAD(vkCmdPipelineBarrier);LOAD(vkCmdBindPipeline);LOAD(vkCmdBindDescriptorSets);LOAD(vkCmdDispatch);LOAD(vkCreateFence);LOAD(vkDestroyFence);LOAD(vkQueueSubmit);LOAD(vkWaitForFences);
 g.vkGetDeviceQueue(g.device,family,0,&queue);
 CHECK(make_buffer(&input_buffer,(1+RELATIVE_RECORDS*16+RELATIVE_RECORDS*256)*4));
 CHECK(make_buffer(&program_buffer,RELATIVE_RECORDS*84) && make_buffer(&constant_buffer,64));
 CHECK(make_buffer(&output_buffer,RELATIVE_OUTPUTS*32) && make_buffer(&context_buffer,160) && make_buffer(&state_buffer,RELATIVE_OUTPUTS*320*4));
 memset(bindings,0,sizeof(bindings));for(i=0;i<6;++i){bindings[i].binding=i;bindings[i].descriptorCount=1;bindings[i].descriptorType=i==4?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}
 set_info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;set_info.bindingCount=6;set_info.pBindings=bindings;OK(g.vkCreateDescriptorSetLayout(g.device,&set_info,NULL,&set_layout));
 layout_info.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;layout_info.setLayoutCount=1;layout_info.pSetLayouts=&set_layout;OK(g.vkCreatePipelineLayout(g.device,&layout_info,NULL,&layout));
 module_info.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;module_info.codeSize=code.count*4;module_info.pCode=code.words;OK(g.vkCreateShaderModule(g.device,&module_info,NULL,&module));
 pipeline_info.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;pipeline_info.layout=layout;pipeline_info.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
 pipeline_info.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pipeline_info.stage.module=module;pipeline_info.stage.pName="main";OK(g.vkCreateComputePipelines(g.device,VK_NULL_HANDLE,1,&pipeline_info,NULL,&pipeline));
 pool_info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pool_info.maxSets=1;pool_info.poolSizeCount=2;pool_info.pPoolSizes=pool_size;OK(g.vkCreateDescriptorPool(g.device,&pool_info,NULL,&pool));
 set_allocate.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;set_allocate.descriptorPool=pool;set_allocate.descriptorSetCount=1;set_allocate.pSetLayouts=&set_layout;OK(g.vkAllocateDescriptorSets(g.device,&set_allocate,&set));
 memset(infos,0,sizeof(infos));infos[0].buffer=input_buffer.buffer;infos[0].range=RELATIVE_INPUT_BYTES;infos[1].buffer=program_buffer.buffer;infos[1].range=RELATIVE_RECORDS*84;
 infos[2].buffer=constant_buffer.buffer;infos[2].range=64;infos[3].buffer=output_buffer.buffer;infos[3].range=RELATIVE_OUTPUTS*32;
 infos[4].buffer=context_buffer.buffer;infos[4].range=160;infos[5].buffer=state_buffer.buffer;infos[5].range=RELATIVE_OUTPUTS*320*4;
 memset(writes,0,sizeof(writes));for(i=0;i<6;++i){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=set;writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=bindings[i].descriptorType;writes[i].pBufferInfo=&infos[i];}
 g.vkUpdateDescriptorSets(g.device,6,writes,0,NULL);
 command_pool_info.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;command_pool_info.queueFamilyIndex=family;command_pool_info.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;OK(g.vkCreateCommandPool(g.device,&command_pool_info,NULL,&command_pool));
 command_allocate.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;command_allocate.commandPool=command_pool;command_allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;command_allocate.commandBufferCount=1;OK(g.vkAllocateCommandBuffers(g.device,&command_allocate,&command));
 fence_info.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;OK(g.vkCreateFence(g.device,&fence_info,NULL,&fence));
 in=(uint32_t*)input_buffer.mapped;out=(uint32_t*)output_buffer.mapped;programs=(uint32_t*)program_buffer.mapped;constants=(uint32_t*)constant_buffer.mapped;context=(uint32_t*)context_buffer.mapped;state=(uint32_t*)state_buffer.mapped;
 for(lane=0;lane<4;++lane){constants[lane]=0x3f800000u;constants[4+lane]=0x40000000u;constants[8+lane]=0x40800000u;constants[12+lane]=0;}
 /* Distinct loop contexts share one descriptor binding, so replay one case at
  * a time. Both endian lanes run together, without a per-continuation readback. */
 for(which=0;which<RELATIVE_CASES;++which)for(budget=1;budget<=7;budget+=6){
  memset(in,0,(size_t)RELATIVE_INPUT_BYTES);memset(state,0,(size_t)RELATIVE_OUTPUTS*320*4);memset(out,0x6d,(size_t)RELATIVE_OUTPUTS*32);in[0]=2;
  relative_context(context,which);relative_program(native_code,which);
  for(i=0;i<2;++i){
   descriptor=in+1+i*16;temporary=in+1+RELATIVE_RECORDS*16+i*256;
   descriptor[0]=i*21;descriptor[1]=21u|(i?0x80000000u:0);descriptor[2]=1+RELATIVE_RECORDS*16+i*256;descriptor[3]=64;
   descriptor[5]=4;descriptor[7]=3;descriptor[8]=1;descriptor[9]=budget;
   for(lane=0;lane<21;++lane)programs[i*21+lane]=i?reverse(native_code[lane]):native_code[lane];
   for(lane=0;lane<256;++lane){float f=(float)(lane/4+1);memcpy(temporary+lane,&f,4);}
  }
  CHECK(cache_buffer(&input_buffer,0) && cache_buffer(&program_buffer,0) && cache_buffer(&constant_buffer,0) && cache_buffer(&context_buffer,0) && cache_buffer(&output_buffer,0) && cache_buffer(&state_buffer,0));
  begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;OK(g.vkBeginCommandBuffer(command,&begin));
  g.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);g.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);
  barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
  g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,NULL,0,NULL);
  for(pass=0;pass<64;++pass){
   g.vkCmdDispatch(command,RELATIVE_OUTPUTS,1,1);barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
   g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,NULL,0,NULL);
  }
  barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
  g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);OK(g.vkEndCommandBuffer(command));
  submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&command;OK(g.vkQueueSubmit(queue,1,&submit,fence));submitted=1;
  OK(g.vkWaitForFences(g.device,1,&fence,VK_TRUE,UINT64_C(10000000000)));submitted=0;
  CHECK(cache_buffer(&output_buffer,1) && cache_buffer(&state_buffer,1));status=relative_status(which);
  for(i=0;i<2;++i){
   if(out[i*8+4]!=status){fprintf(stderr,"relative case=%u budget=%u endian=%u status=%u expected=%u\n",which,budget,i,out[i*8+4],status);goto done;}
   if(status==1){
#ifndef XE_RELATIVE_TEST_LITERALS_ONLY
    descriptor=in+1+i*16;CHECK(xe_alu_control_reference(native_code,in+descriptor[2],64,constants,4,0,3,reference,context)==1);++native;
    for(lane=0;lane<4;++lane)CHECK(out[i*8+lane]==reference[lane]);
#endif
    for(lane=0;lane<4;++lane)CHECK(out[i*8+lane]==relative_golden(which,lane));
    ++literals;
    CHECK(out[i*8+5]==1 && out[i*8+6]==15);
   }else for(lane=0;lane<7;++lane)if(lane!=4)CHECK(out[i*8+lane]==0x6d6d6d6du);
   CHECK(out[i*8+7]==0x6d6d6d6du && state[i*320]==1 && state[i*320+1]==status);
  }
  for(i=2;i<RELATIVE_OUTPUTS;++i){for(lane=0;lane<8;++lane)CHECK(out[i*8+lane]==0x6d6d6d6du);for(lane=0;lane<320;++lane)CHECK(state[i*320+lane]==0);}
  relative_context(bank,which);CHECK(!memcmp(bank,context,160));
  for(i=0;i<2;++i)for(lane=0;lane<21;++lane)CHECK(programs[i*21+lane]==(i?reverse(native_code[lane]):native_code[lane]));
  CHECK(cache_buffer(&input_buffer,1));
  for(i=0;i<2;++i)for(lane=0;lane<256;++lane){float f=(float)(lane/4+1);uint32_t bits;memcpy(&bits,&f,4);CHECK(in[1+RELATIVE_RECORDS*16+i*256+lane]==bits);}
  LOAD(vkResetCommandPool);LOAD(vkResetFences);OK(g.vkResetFences(g.device,1,&fence));OK(g.vkResetCommandPool(g.device,command_pool,0));
 }
 CHECK(literals==180);
#ifndef XE_RELATIVE_TEST_LITERALS_ONLY
 CHECK(native==180);
#else
 CHECK(native==0);
#endif
 printf("PASS: relative writes cases=48 endian_modes=2 budgets=2 native=%u literals=%u pipelines=1 between_pass_readbacks=0\n",native,literals);result=0;
done:
 if(submitted)return 1; /* Test process exit owns cleanup after a failed fence. */
 if(g.device){if(fence)g.vkDestroyFence(g.device,fence,NULL);if(command_pool)g.vkDestroyCommandPool(g.device,command_pool,NULL);if(pool)g.vkDestroyDescriptorPool(g.device,pool,NULL);if(pipeline)g.vkDestroyPipeline(g.device,pipeline,NULL);if(module)g.vkDestroyShaderModule(g.device,module,NULL);if(layout)g.vkDestroyPipelineLayout(g.device,layout,NULL);if(set_layout)g.vkDestroyDescriptorSetLayout(g.device,set_layout,NULL);
  close_buffer(&input_buffer);close_buffer(&program_buffer);close_buffer(&constant_buffer);close_buffer(&output_buffer);close_buffer(&context_buffer);close_buffer(&state_buffer);g.vkDestroyDevice(g.device,NULL);}
 if(g.instance)g.vkDestroyInstance(g.instance,NULL);
 xe_spv_binary_close(&code);
#ifdef _WIN32
 if(loader)FreeLibrary(loader);
#else
 if(loader)dlclose(loader);
#endif
 return result;
}
