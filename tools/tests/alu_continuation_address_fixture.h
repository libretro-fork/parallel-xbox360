/* Borrowed continuation-address fixture driver. MSVC C89; tests only. */
#ifndef XE_TEST_ALU_CONTINUATION_ADDRESS_FIXTURE_H
#define XE_TEST_ALU_CONTINUATION_ADDRESS_FIXTURE_H
#define main xe_address_original_continuation_fixture
#include "core_alu_resume_vulkan_test.c"
#undef main
enum {RELATIVE_RECORDS=96,RELATIVE_OUTPUTS=98,RELATIVE_INPUT_BYTES=(1+RELATIVE_RECORDS*16+RELATIVE_RECORDS*256)*4};
typedef struct address_fixture {
 const char *name,*device;
 unsigned cases,comparisons,native_comparisons,reference_rejections;
 void (*program)(uint32_t*,unsigned);
 void (*context)(uint32_t*,unsigned);
 unsigned (*status)(unsigned);
 uint32_t (*golden)(unsigned,unsigned);
 uint32_t (*constant_word)(unsigned);
 void (*window)(uint32_t*,unsigned);
} address_fixture;
static int address_fixture_run(const address_fixture *test)
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
 uint32_t native_code[21],reference[7],bank[40];unsigned which,status,budget,native=0,literals=0,rejected=0,reference_status;float priority=1.0f;int result=1,submitted=0;
 const char *validation=getenv("XE_RELATIVE_GPU_VALIDATION");const char *layer="VK_LAYER_KHRONOS_validation";
 (void)reference;(void)reference_status;
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
 fprintf(stderr,"%s vendor=%u device=%u driver=%u name=%s validation=%u\n",test->device,properties.vendorID,properties.deviceID,properties.driverVersion,properties.deviceName,instance.enabledLayerCount);
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
 CHECK(make_buffer(&program_buffer,RELATIVE_RECORDS*84) && make_buffer(&constant_buffer,8192));
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
 infos[2].buffer=constant_buffer.buffer;infos[2].range=8192;infos[3].buffer=output_buffer.buffer;infos[3].range=RELATIVE_OUTPUTS*32;
 infos[4].buffer=context_buffer.buffer;infos[4].range=160;infos[5].buffer=state_buffer.buffer;infos[5].range=RELATIVE_OUTPUTS*320*4;
 memset(writes,0,sizeof(writes));for(i=0;i<6;++i){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=set;writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=bindings[i].descriptorType;writes[i].pBufferInfo=&infos[i];}
 g.vkUpdateDescriptorSets(g.device,6,writes,0,NULL);
 command_pool_info.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;command_pool_info.queueFamilyIndex=family;command_pool_info.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;OK(g.vkCreateCommandPool(g.device,&command_pool_info,NULL,&command_pool));
 command_allocate.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;command_allocate.commandPool=command_pool;command_allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;command_allocate.commandBufferCount=1;OK(g.vkAllocateCommandBuffers(g.device,&command_allocate,&command));
 fence_info.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;OK(g.vkCreateFence(g.device,&fence_info,NULL,&fence));
 in=(uint32_t*)input_buffer.mapped;out=(uint32_t*)output_buffer.mapped;programs=(uint32_t*)program_buffer.mapped;constants=(uint32_t*)constant_buffer.mapped;context=(uint32_t*)context_buffer.mapped;state=(uint32_t*)state_buffer.mapped;
 for(lane=0;lane<2048;++lane)constants[lane]=test->constant_word(lane);
 /* Distinct loop contexts share one descriptor binding, so replay one case at
  * a time. Both endian lanes run together, without a per-continuation readback. */
 for(which=0;which<test->cases;++which)for(budget=1;budget<=7;budget+=6){
  memset(in,0,(size_t)RELATIVE_INPUT_BYTES);memset(state,0,(size_t)RELATIVE_OUTPUTS*320*4);memset(out,0x6d,(size_t)RELATIVE_OUTPUTS*32);in[0]=2;
  test->context(context,which);test->program(native_code,which);
  for(i=0;i<2;++i){
   descriptor=in+1+i*16;temporary=in+1+RELATIVE_RECORDS*16+i*256;
   descriptor[0]=i*21;descriptor[1]=21u|(i?0x80000000u:0);descriptor[2]=1+RELATIVE_RECORDS*16+i*256;descriptor[3]=64;
   test->window(descriptor,which);descriptor[8]=1;descriptor[9]=budget;
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
  CHECK(cache_buffer(&output_buffer,1) && cache_buffer(&state_buffer,1));status=test->status(which);
  for(i=0;i<2;++i){
   if(out[i*8+4]!=status){fprintf(stderr,"relative case=%u budget=%u endian=%u status=%u expected=%u\n",which,budget,i,out[i*8+4],status);goto done;}
   if(status==1){
#ifndef XE_RELATIVE_TEST_LITERALS_ONLY
    descriptor=in+1+i*16;memset(reference,0x5a,sizeof(reference));
    reference_status=xe_alu_control_reference(native_code,in+descriptor[2],64,constants,descriptor[5],descriptor[6],descriptor[7],reference,context);
    if(descriptor[6]>511 || descriptor[7]>511){
     CHECK(reference_status==0);for(lane=0;lane<7;++lane)CHECK(reference[lane]==0x5a5a5a5au);++rejected;
    }else{
    CHECK(reference_status==1);++native;
    for(lane=0;lane<4;++lane)if(out[i*8+lane]!=reference[lane]){
     fprintf(stderr,"%s case=%u budget=%u endian=%u lane=%u GPU=%08x native=%08x\n",test->name,which,budget,i,lane,out[i*8+lane],reference[lane]);goto done;
    }
    }
#endif
    for(lane=0;lane<4;++lane)if(out[i*8+lane]!=test->golden(which,lane)){
     fprintf(stderr,"%s case=%u budget=%u endian=%u lane=%u GPU=%08x literal=%08x\n",test->name,which,budget,i,lane,out[i*8+lane],test->golden(which,lane));goto done;
    }
    ++literals;
    CHECK(out[i*8+5]==1 && out[i*8+6]==15);
   }else for(lane=0;lane<7;++lane)if(lane!=4)CHECK(out[i*8+lane]==0x6d6d6d6du);
   CHECK(out[i*8+7]==0x6d6d6d6du && state[i*320]==1 && state[i*320+1]==status);
  }
  for(i=2;i<RELATIVE_OUTPUTS;++i){for(lane=0;lane<8;++lane)CHECK(out[i*8+lane]==0x6d6d6d6du);for(lane=0;lane<320;++lane)CHECK(state[i*320+lane]==0);}
  test->context(bank,which);CHECK(!memcmp(bank,context,160));
  CHECK(cache_buffer(&constant_buffer,1));for(i=0;i<2048;++i)CHECK(constants[i]==test->constant_word(i));
  for(i=0;i<2;++i)for(lane=0;lane<21;++lane)CHECK(programs[i*21+lane]==(i?reverse(native_code[lane]):native_code[lane]));
  CHECK(cache_buffer(&input_buffer,1));
  for(i=0;i<2;++i)for(lane=0;lane<256;++lane){float f=(float)(lane/4+1);uint32_t bits;memcpy(&bits,&f,4);CHECK(in[1+RELATIVE_RECORDS*16+i*256+lane]==bits);}
  LOAD(vkResetCommandPool);LOAD(vkResetFences);OK(g.vkResetFences(g.device,1,&fence));OK(g.vkResetCommandPool(g.device,command_pool,0));
 }
 CHECK(literals==test->comparisons);
#ifndef XE_RELATIVE_TEST_LITERALS_ONLY
 CHECK(native==test->native_comparisons && rejected==test->reference_rejections);
#else
 CHECK(native==0 && rejected==0);
#endif
 printf("PASS: %s cases=%u endian_modes=2 budgets=2 native=%u literals=%u pipelines=1 between_pass_readbacks=0\n",test->name,test->cases,native,literals);result=0;
 printf("REFERENCE_DOMAIN rejections=%u expected=%u literals_only=%u\n",rejected,test->reference_rejections,
#ifdef XE_RELATIVE_TEST_LITERALS_ONLY
  1u
#else
  0u
#endif
 );
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

#endif
