/* One raw interpreter PSO, mutable instruction data, exact RTZ/FTZ oracles. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "libretro/core_alu_spv.h"
#if XE_ALU_PROGRAM_ABI != 1
#error The frozen packed-program-v1 fixture needs descriptor ABI 1
#endif
#include "libretro/core_float_rtz.h"
#ifdef XE_ALU_CONTROL_TEST
#define XE_VK_TEST_UNIFORM
#endif
#ifdef XE_SHADER_COMPILE_BENCH
#define XE_VK_TEST_GPU_TIMING
#include "gpu_timestamp_measure.h"
#endif
#include "vulkan_test_context.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"ALU GPU line %u\n",(unsigned)__LINE__);goto done;}}while(0)
enum {RECORDS=259,OUTPUT_RECORDS=265,
 INPUT_WORDS=1+RECORDS*8+RECORDS*256,PROGRAM_WORDS=((RECORDS+1)/2)*21};
static struct buffer input_buffer,program_buffer,constant_buffer,output_buffer;
#ifdef XE_ALU_CONTROL_TEST
static struct buffer context_buffer;
#endif
/* Packed fixtures, including literal coissue/mask anchors; no guest assets. */
unsigned xe_alu_program_reference(const uint32_t*,const uint32_t*,unsigned,
 const uint32_t*,unsigned,unsigned,unsigned,uint32_t*);
unsigned xe_alu_control_reference(const uint32_t*,const uint32_t*,unsigned,
 const uint32_t*,unsigned,unsigned,unsigned,uint32_t*,const uint32_t*);
void xe_alu_program_export(uint32_t *out,unsigned target,const void *value,uint32_t mask) {
 unsigned i;if(target!=62){out[5]=10000;return;}
 for(i=0;i<4;++i)if(mask&(1u<<i))memcpy(out+i,(const unsigned char*)value+i*4,4);
 out[6]|=mask;++out[5];
}
static void alu(uint32_t *p,unsigned destination,unsigned vm,unsigned sm,
 unsigned sop,unsigned vop,unsigned a,unsigned b,unsigned c,unsigned temporary) {
 p[0]=destination|(vm<<16)|(sm<<20)|(sop<<26);p[1]=0;
 p[2]=(a<<16)|(b<<8)|c|(vop<<24)|((temporary&1)?0x80000000u:0)|
  ((temporary&2)?0x40000000u:0)|((temporary&4)?0x20000000u:0);
}
#ifndef XE_ALU_CONTROL_TEST
static void program(uint32_t *p,unsigned group) {
 unsigned kind=group%32,i,j,count=1;uint32_t *q=p+3;
 memset(p,0,21*4);alu(q,62,15,0,50,0,0,1,2,0);q[0]|=1u<<15;
 switch(kind){
 case 1:alu(q,62,15,0,50,1,0,1,2,0);q[0]|=1u<<15;break;
 case 2:alu(q,62,15,0,50,11,0,1,2,0);q[0]|=1u<<15;break;
 case 3:alu(q,62,0,15,0,0,0,1,2,0);q[0]|=1u<<15;break;
 case 4:alu(q,62,0,15,44,0,0,1,2,(group/32)&1?4:0);q[0]|=1u<<15;break;
 case 5:case 6:
  count=2;alu(q,0,kind==5?5:15,kind==5?10:5,0,kind==6?11:0,0,0,0,5);
  alu(q+3,62,15,0,50,0,0,3,0,1);q[3]|=1u<<15;break;
 case 7:
  count=6;alu(q,0,15,0,50,0,0,1,2,0);
  for(i=1;i<5;++i)alu(q+3*i,i,15,0,50,i%2?11:1,i-1,1,2,1);
  alu(q+15,62,15,0,50,0,4,3,0,1);q[15]|=1u<<15;break;
 case 8:
  count=2;alu(q,62,5,0,50,0,0,3,0,1);q[0]|=1u<<15;
  alu(q+3,62,10,0,50,0,1,3,0,1);q[3]|=1u<<15;break;
 case 9:q[0]|=15u<<20;break;
 case 10:q[0]&=~(15u<<16);q[0]|=1u<<14;break;
 case 11:
  count=2;alu(q,0,0,0,0,0,0,1,2,0);
  alu(q+3,62,0,15,50,0,0,1,2,0);q[3]|=1u<<15;break;
 case 14:alu(q,62,15,0,50,0,63,3,0,1);q[0]|=1u<<15;break;
 case 15:count=0;break;
 case 22:break;
 case 23:q[0]&=~(10u<<16);break;
 case 24:count=2;memcpy(q+3,q,12);q[5]=(q[5]&0xe0ffffffu)|(31u<<24);break;
 case 25:q[1]|=1u<<28;break;
 case 26:q[1]|=1u<<31;break;
 case 27:q[0]|=1u<<24;break;
 case 28:q[2]|=0x80400000u;break;
 case 29:q[2]=(q[2]&0xe0ffffffu)|(31u<<24);break;
 case 30:q[0]=(q[0]&0x03ffffffu)|(51u<<26);break;
 default:break;
 }
 p[0]=1u|(count<<12);p[1]=(kind==22?1u:2u)<<12;
 if(kind==31)p[0]|=1u<<16;
 /* Later groups exercise all modifier bits and nonuniform source swizzles. */
 if(group>=32 && kind<15)for(i=0;i<count;++i){
  q[i*3]|=((group>>5)&1)<<7;
  q[i*3+1]|=((group*0x71b359u)&0xffffffu)|(((group>>5)&7)<<24);
  for(j=0;j<3;++j)if(((q[i*3+2]>>(31-j))&1) && ((group>>(5+j))&1))q[i*3+2]|=1u<<(23-j*8);
 }
}
static unsigned expected_status(unsigned kind) {
 if(kind==15 || kind==23)return 4;
 if(kind>=16 && kind<=21)return 2;
 if(kind>=22)return 3;
 return 1;
}
#else
#include "shader_control_program_fixture.h"
#include "shader_register_borrow_fixture.h"
#include "shader_minmax_fixture.h"
#include "shader_dot_fixture.h"
#endif
#ifdef XE_ALU_CONTROL_TEST
#define XE_PROGRAM_BINDINGS 5
#define XE_PROGRAM_BUILD xe_alu_control_spv_build
#define XE_PROGRAM_STAGE(x) "raw_control." x
#define XE_PROGRAM_DEVICE "RAW_CONTROL_DEVICE"
#else
#define XE_PROGRAM_BINDINGS 4
#define XE_PROGRAM_BUILD xe_alu_program_spv_build
#define XE_PROGRAM_STAGE(x) "raw_program." x
#define XE_PROGRAM_DEVICE "RAW_PROGRAM_DEVICE"
#endif
#ifndef XE_BOOL_CASES
#define XE_BOOL_CASES 0
#endif
#ifndef XE_LOOP_CASES
#define XE_LOOP_CASES 0
#endif
#ifndef XE_PRED_CASES
#define XE_PRED_CASES 0
#endif
#ifndef XE_COMPARE_CASES
#define XE_COMPARE_CASES 0
#endif
#ifndef XE_LAZY_CASES
#define XE_LAZY_CASES 0
#endif
#ifndef XE_REGISTER_CASES
#define XE_REGISTER_CASES 0
#endif
#ifndef XE_MINMAX_CASES
#define XE_MINMAX_CASES 0
#endif
#define XE_COMPARE_FIRST (XE_BOOL_CASES+XE_LOOP_CASES+XE_PRED_CASES)
#define XE_COMPARE_GOLDEN_FIRST 4096
#define XE_LAZY_FIRST (XE_COMPARE_FIRST+XE_COMPARE_CASES)
#define XE_LAZY_GOLDEN_FIRST 8192
#define XE_REGISTER_FIRST (XE_LAZY_FIRST+XE_LAZY_CASES)
#define XE_REGISTER_GOLDEN_FIRST 16384
#define XE_MINMAX_FIRST (XE_REGISTER_FIRST+XE_REGISTER_CASES)
#define XE_MINMAX_GOLDEN_FIRST 32768
#ifndef XE_DOT_CASES
#define XE_DOT_CASES 0
#endif
#define XE_DOT_FIRST (XE_MINMAX_FIRST+XE_MINMAX_CASES)
#define XE_DOT_GOLDEN_FIRST 65536
#define XE_EXTRA_CASES (XE_DOT_FIRST+XE_DOT_CASES)
static void round_program(uint32_t *p,unsigned group,unsigned round) {
#if XE_DOT_CASES
 if(round>=3+XE_DOT_FIRST){dot_program(p,round-3-XE_DOT_FIRST);return;}
#endif
#if XE_MINMAX_CASES
 if(round>=3+XE_MINMAX_FIRST){minmax_program(p,round-3-XE_MINMAX_FIRST);return;}
#endif
#if XE_REGISTER_CASES
 if(round>=3+XE_REGISTER_FIRST){register_program(p,round-3-XE_REGISTER_FIRST);return;}
#endif
 (void)round;
#if XE_LAZY_CASES
 if(round>=3+XE_LAZY_FIRST){lazy_program(p,round-3-XE_LAZY_FIRST);return;}
#endif
#if XE_COMPARE_CASES
 if(round>=3+XE_COMPARE_FIRST){compare_program(p,round-3-XE_COMPARE_FIRST);return;}
#endif
#if XE_PRED_CASES
 if(round>=3+XE_BOOL_CASES+XE_LOOP_CASES){pred_program(p,pred_case(round-3-XE_BOOL_CASES-XE_LOOP_CASES,0));return;}
#endif
#if XE_LOOP_CASES
 if(round>=3+XE_BOOL_CASES){loop_program(p,round-3-XE_BOOL_CASES);return;}
#endif
#if XE_BOOL_CASES
 if(round>=3){bool_program(p,round-3);return;}
#endif
 program(p,group);
}
#if XE_EXTRA_CASES
static unsigned extra_which(unsigned which,unsigned invocation) {
#if XE_DOT_CASES
 if(which>=XE_DOT_FIRST)return XE_DOT_GOLDEN_FIRST+which-XE_DOT_FIRST;
#endif
#if XE_MINMAX_CASES
 if(which>=XE_MINMAX_FIRST)return XE_MINMAX_GOLDEN_FIRST+which-XE_MINMAX_FIRST;
#endif
#if XE_REGISTER_CASES
 if(which>=XE_REGISTER_FIRST)return XE_REGISTER_GOLDEN_FIRST+which-XE_REGISTER_FIRST;
#endif
 (void)invocation;
#if XE_LAZY_CASES
 if(which>=XE_LAZY_FIRST)return XE_LAZY_GOLDEN_FIRST+which-XE_LAZY_FIRST;
#endif
#if XE_COMPARE_CASES
 if(which>=XE_COMPARE_FIRST)return XE_COMPARE_GOLDEN_FIRST+which-XE_COMPARE_FIRST;
#endif
#if XE_PRED_CASES
 if(which>=XE_BOOL_CASES+XE_LOOP_CASES)
  return XE_BOOL_CASES+XE_LOOP_CASES+pred_case(which-XE_BOOL_CASES-XE_LOOP_CASES,invocation);
#endif
 return which;
}
static void extra_context(uint32_t *bank,unsigned which) {
#if XE_DOT_CASES
 if(which>=XE_DOT_FIRST){dot_context(bank,which-XE_DOT_FIRST);return;}
#endif
#if XE_MINMAX_CASES
 if(which>=XE_MINMAX_FIRST){minmax_context(bank,which-XE_MINMAX_FIRST);return;}
#endif
#if XE_REGISTER_CASES
 if(which>=XE_REGISTER_FIRST){memset(bank,0,160);return;}
#endif
#if XE_LAZY_CASES
 if(which>=XE_LAZY_FIRST){lazy_context(bank);return;}
#endif
#if XE_COMPARE_CASES
 if(which>=XE_COMPARE_FIRST){memset(bank,0,160);return;}
#endif
#if XE_PRED_CASES
 if(which>=XE_BOOL_CASES+XE_LOOP_CASES){pred_context(bank,pred_case(which-XE_BOOL_CASES-XE_LOOP_CASES,0));return;}
#endif
#if XE_LOOP_CASES
 if(which>=XE_BOOL_CASES){loop_context(bank,which-XE_BOOL_CASES);return;}
#endif
#if XE_BOOL_CASES
 bool_context(bank,which);
#endif
}
static unsigned extra_status(unsigned which) {
#if XE_DOT_CASES
 if(which>=XE_DOT_GOLDEN_FIRST)return dot_status(which-XE_DOT_GOLDEN_FIRST);
#endif
#if XE_MINMAX_CASES
 if(which>=XE_MINMAX_GOLDEN_FIRST)return minmax_status(which-XE_MINMAX_GOLDEN_FIRST);
#endif
#if XE_REGISTER_CASES
 if(which>=XE_REGISTER_GOLDEN_FIRST)return 1;
#endif
#if XE_LAZY_CASES
 if(which>=XE_LAZY_GOLDEN_FIRST)return lazy_status(which-XE_LAZY_GOLDEN_FIRST);
#endif
#if XE_COMPARE_CASES
 if(which>=XE_COMPARE_GOLDEN_FIRST)return 1;
#endif
#if XE_PRED_CASES
 if(which>=XE_BOOL_CASES+XE_LOOP_CASES)return pred_status(which-XE_BOOL_CASES-XE_LOOP_CASES);
#endif
#if XE_LOOP_CASES
 if(which>=XE_BOOL_CASES)return loop_status(which-XE_BOOL_CASES);
#endif
#if XE_BOOL_CASES
 return bool_status(which);
#else
 return 0;
#endif
}
static uint32_t extra_golden(unsigned which,unsigned lane) {
#if XE_DOT_CASES
 if(which>=XE_DOT_GOLDEN_FIRST)return dot_golden(which-XE_DOT_GOLDEN_FIRST,lane);
#endif
#if XE_MINMAX_CASES
 if(which>=XE_MINMAX_GOLDEN_FIRST)return minmax_golden(which-XE_MINMAX_GOLDEN_FIRST,lane);
#endif
#if XE_REGISTER_CASES
 if(which>=XE_REGISTER_GOLDEN_FIRST)return register_golden(which-XE_REGISTER_GOLDEN_FIRST,lane);
#endif
 (void)lane;
#if XE_LAZY_CASES
 if(which>=XE_LAZY_GOLDEN_FIRST)return lazy_golden(which-XE_LAZY_GOLDEN_FIRST);
#endif
#if XE_COMPARE_CASES
 if(which>=XE_COMPARE_GOLDEN_FIRST)return compare_golden(which-XE_COMPARE_GOLDEN_FIRST,lane);
#endif
#if XE_PRED_CASES
 if(which>=XE_BOOL_CASES+XE_LOOP_CASES)return pred_golden(which-XE_BOOL_CASES-XE_LOOP_CASES,lane);
#endif
#if XE_LOOP_CASES
 if(which>=XE_BOOL_CASES)return loop_golden(which-XE_BOOL_CASES,lane);
#endif
#if XE_BOOL_CASES
 return bool_golden(which);
#else
 return 0;
#endif
}
#endif
static int round_swap(unsigned round,unsigned group) {
#if XE_PRED_CASES
 if(round>=3+XE_BOOL_CASES+XE_LOOP_CASES)return (round-3-XE_BOOL_CASES-XE_LOOP_CASES)&1;
#endif
#if XE_LOOP_CASES
 if(round>=3+XE_BOOL_CASES)return (round-3-XE_BOOL_CASES)&1;
#endif
 return round>=3?((round-3)>>1)&1:round==1 || (round==2 && !(group&1));
}
static uint32_t next(uint32_t *state){*state=*state*1664525u+1013904223u;return *state;}
static int same(uint32_t a,uint32_t b){return a==b || ((a&0x7fffffffu)>0x7f800000u && (b&0x7fffffffu)>0x7f800000u);}
static uint32_t reverse(uint32_t x){return (x<<24)|((x&0xff00u)<<8)|((x>>8)&0xff00u)|(x>>24);}
static int capture_module(const xe_spv_binary *code) {
 const char *path=getenv("XE_ALU_PROGRAM_DUMP");FILE *file;
 if(!path || !*path)return 1;
 file=fopen(path,"wb");if(!file)return 0;
 if(fwrite(code->words,4,code->count,file)!=code->count){fclose(file);return 0;}
 return !fclose(file);
}
#ifdef XE_SHADER_COMPILE_BENCH
static int gpu_timing_record(const char *path,const char *stage,unsigned which,
 const uint64_t ticks[4],unsigned bits,float period,double host_seconds,double seconds) {
 char raw_path[4096];size_t length=strlen(path);FILE *file;int good;
 if(length>=sizeof(raw_path)-10)return 0;
 memcpy(raw_path,path,length);memcpy(raw_path+length,".ticks.csv",11);
 file=fopen(path,"ab");if(!file)return 0;
 good=fprintf(file,"%s,%u,%.17g\n",stage,which,seconds)>=0;
 if(fclose(file))good=0;
 if(!good)return 0;
 file=fopen(raw_path,"ab");if(!file)return 0;
 good=fprintf(file,"%s,%u,%08lx%08lx,%08lx%08lx,%08lx%08lx,%08lx%08lx,%u,%.17g,%.17g\n",stage,which,
  (unsigned long)(uint32_t)(ticks[0]>>32),(unsigned long)(uint32_t)ticks[0],
  (unsigned long)(uint32_t)(ticks[2]>>32),(unsigned long)(uint32_t)ticks[2],
  (unsigned long)(uint32_t)(ticks[1]>>32),(unsigned long)(uint32_t)ticks[1],
  (unsigned long)(uint32_t)(ticks[3]>>32),(unsigned long)(uint32_t)ticks[3],bits,(double)period,host_seconds)>=0;
 if(fclose(file))good=0;
 return good;
}
#endif
int main(void){
#ifdef _WIN32
 HMODULE loader=LoadLibraryA("vulkan-1.dll");FARPROC entry;
#else
 void *loader=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL),*entry;
#endif
 static VkPhysicalDevice physical[8];static VkQueueFamilyProperties families[32];
 static VkPhysicalDeviceProperties properties;
 VkApplicationInfo app={0};VkInstanceCreateInfo instance={0};VkDeviceQueueCreateInfo queue_info={0};VkDeviceCreateInfo device_info={0};
 VkQueue queue=VK_NULL_HANDLE;VkDescriptorSetLayoutBinding bindings[XE_PROGRAM_BINDINGS];VkDescriptorSetLayoutCreateInfo set_info={0};
 VkDescriptorSetLayout set_layout=VK_NULL_HANDLE;VkPipelineLayoutCreateInfo layout_info={0};VkPipelineLayout layout=VK_NULL_HANDLE;
 VkShaderModuleCreateInfo module_info={0};VkShaderModule module=VK_NULL_HANDLE;VkComputePipelineCreateInfo pipeline_info={0};VkPipeline pipeline=VK_NULL_HANDLE;
#ifdef XE_SHADER_COMPILE_BENCH
 VkPipelineCacheCreateInfo pipeline_cache_info={0};VkPipelineCache pipeline_cache=VK_NULL_HANDLE;
 VkPipeline first_pipeline=VK_NULL_HANDLE;
 VkQueryPool query_pool=VK_NULL_HANDLE;VkQueryPoolCreateInfo query_info={0};
 VkSemaphore timestamp_semaphore=VK_NULL_HANDLE;VkSemaphoreCreateInfo semaphore_info={0};
 VkSemaphore completion_semaphore=VK_NULL_HANDLE;
 VkCommandBuffer timestamp_command=VK_NULL_HANDLE,completion_command=VK_NULL_HANDLE;VkSubmitInfo gpu_submits[3]={{0},{0},{0}};
 VkPipelineStageFlags gpu_wait_stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
 uint64_t gpu_ticks[4];double gpu_started=0,gpu_host_seconds=0,gpu_seconds;
 unsigned gpu_dispatches=0;
#endif
 VkDescriptorPoolSize pool_size[2]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,4},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1}};VkDescriptorPoolCreateInfo pool_info={0};VkDescriptorPool pool=VK_NULL_HANDLE;
 VkDescriptorSetAllocateInfo set_allocate={0};VkDescriptorSet set=VK_NULL_HANDLE;VkDescriptorBufferInfo infos[XE_PROGRAM_BINDINGS];VkWriteDescriptorSet writes[XE_PROGRAM_BINDINGS];
 VkCommandPoolCreateInfo command_pool_info={0};VkCommandPool command_pool=VK_NULL_HANDLE;VkCommandBufferAllocateInfo command_allocate={0};
 VkCommandBuffer command;VkCommandBufferBeginInfo begin={0};VkMemoryBarrier barrier={0};VkFenceCreateInfo fence_info={0};VkFence fence=VK_NULL_HANDLE;VkSubmitInfo submit={0};
 VkResult completion;
 xe_spv_binary code={0};uint32_t count,family,i,lane,round,limit,output_limit,*in,*out,*programs,*constants,*descriptor,*temporary,reference[7],reference_code[21],seed=0x81732019u;
 unsigned kind,group,status,compared=0;
#if XE_COMPARE_CASES
 unsigned comparison_invocations=0;
#endif
#if XE_LAZY_CASES
 unsigned lazy_invocations=0;
#endif
#if XE_REGISTER_CASES
 unsigned register_invocations=0;
#endif
#if XE_MINMAX_CASES
 unsigned minmax_invocations=0;
#endif
 unsigned dot_invocations=0;
 unsigned modules=0,pipelines=0,cases=0,inactive=0,context_replays=XE_EXTRA_CASES;float priority=1.0f;int result=1;
 const char *timing_only=getenv("XE_ALU_PROGRAM_TIMING_ONLY");
 const char *valid_mode=getenv("XE_ALU_PROGRAM_VALID_WORK");
 const char *gpu_path=getenv("XE_ALU_GPU_TIMING_CSV");
 const char *gpu_repeat=getenv("XE_ALU_GPU_REPEAT"),*gpu_delay=getenv("XE_ALU_GPU_RECORD_DELAY");
 unsigned gpu_repetitions=1;int gpu_record_delay=0;
 int gpu_timing=gpu_path && *gpu_path;
 int valid_work=valid_mode && !strcmp(valid_mode,"1");
 if(gpu_repeat && *gpu_repeat){CHECK(!strcmp(gpu_repeat,"1") || !strcmp(gpu_repeat,"8"));gpu_repetitions=!strcmp(gpu_repeat,"8")?8u:1u;}
 if(gpu_delay && *gpu_delay){CHECK(!strcmp(gpu_delay,"0") || !strcmp(gpu_delay,"1"));gpu_record_delay=!strcmp(gpu_delay,"1");}
 CHECK(gpu_timing || (gpu_repetitions==1 && !gpu_record_delay));
 if(timing_only && !strcmp(timing_only,"1"))context_replays=0;
 if(valid_work)context_replays=0;
#ifndef XE_SHADER_COMPILE_BENCH
 if(gpu_timing){fprintf(stderr,"GPU timing requires XE_SHADER_COMPILE_BENCH\n");goto done;}
#endif
 (void)shader;CHECK(loader);XE_COMPILE_BEGIN();CHECK(XE_PROGRAM_BUILD(&code));
 XE_COMPILE_END(XE_PROGRAM_STAGE("emit"),0);
 CHECK(capture_module(&code));
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
 fprintf(stderr,"%s%s%s Vulkan vendor=%u device=%u driver=%u name=%s\n",XE_PROGRAM_DEVICE,gpu_timing?"_GPU":"",valid_work?"_VALID":"",properties.vendorID,properties.deviceID,properties.driverVersion,properties.deviceName);
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
#ifdef XE_SHADER_COMPILE_BENCH
 if(gpu_timing){
  CHECK(families[family].timestampValidBits>=36 && families[family].timestampValidBits<=64 && properties.limits.timestampPeriod>0);
  LOAD(vkCreateQueryPool);LOAD(vkDestroyQueryPool);LOAD(vkGetQueryPoolResults);LOAD(vkCmdResetQueryPool);LOAD(vkCmdWriteTimestamp);
  LOAD(vkCreateSemaphore);LOAD(vkDestroySemaphore);
  query_info.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;query_info.queryType=VK_QUERY_TYPE_TIMESTAMP;query_info.queryCount=2;
  OK(g.vkCreateQueryPool(g.device,&query_info,NULL,&query_pool));
  semaphore_info.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  OK(g.vkCreateSemaphore(g.device,&semaphore_info,NULL,&timestamp_semaphore));
  OK(g.vkCreateSemaphore(g.device,&semaphore_info,NULL,&completion_semaphore));
  printf("GPU_TIMESTAMP queue=%u bits=%u period_ns=%.17g repetitions=%u record_delay=%u ordering=semaphore_chain3\n",family,families[family].timestampValidBits,(double)properties.limits.timestampPeriod,gpu_repetitions,(unsigned)gpu_record_delay);
 }
#endif
 g.vkGetDeviceQueue(g.device,family,0,&queue);CHECK(make_buffer(&input_buffer,INPUT_WORDS*4) && make_buffer(&program_buffer,PROGRAM_WORDS*4) && make_buffer(&constant_buffer,512*16) && make_buffer(&output_buffer,OUTPUT_RECORDS*32));
#ifdef XE_ALU_CONTROL_TEST
 CHECK(make_buffer(&context_buffer,160));memset(context_buffer.mapped,0,160);
#endif
 memset(bindings,0,sizeof(bindings));for(i=0;i<XE_PROGRAM_BINDINGS;++i){bindings[i].binding=i;bindings[i].descriptorType=i==4?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}
 set_info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;set_info.bindingCount=XE_PROGRAM_BINDINGS;set_info.pBindings=bindings;OK(g.vkCreateDescriptorSetLayout(g.device,&set_info,NULL,&set_layout));
 layout_info.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;layout_info.setLayoutCount=1;layout_info.pSetLayouts=&set_layout;OK(g.vkCreatePipelineLayout(g.device,&layout_info,NULL,&layout));
 module_info.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;module_info.codeSize=code.count*4;module_info.pCode=code.words;++modules;
 XE_COMPILE_BEGIN();OK(g.vkCreateShaderModule(g.device,&module_info,NULL,&module));XE_COMPILE_END(XE_PROGRAM_STAGE("module"),0);
 pipeline_info.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;pipeline_info.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
 pipeline_info.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pipeline_info.stage.module=module;pipeline_info.stage.pName="main";pipeline_info.layout=layout;
#ifdef XE_SHADER_COMPILE_BENCH
 LOAD(vkCreatePipelineCache);LOAD(vkDestroyPipelineCache);
 pipeline_cache_info.sType=VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
 OK(g.vkCreatePipelineCache(g.device,&pipeline_cache_info,NULL,&pipeline_cache));
#endif
 ++pipelines;XE_COMPILE_BEGIN();
#ifdef XE_SHADER_COMPILE_BENCH
 OK(g.vkCreateComputePipelines(g.device,pipeline_cache,1,&pipeline_info,NULL,&pipeline));
#else
 OK(g.vkCreateComputePipelines(g.device,VK_NULL_HANDLE,1,&pipeline_info,NULL,&pipeline));
#endif
 XE_COMPILE_END(XE_PROGRAM_STAGE("compute_pso"),0);
#ifdef XE_SHADER_COMPILE_BENCH
 /* Retain the first PSO and its explicit private cache for the warm condition.
  * First creation is observed separately: application-cache emptiness does not
  * establish an empty driver cache. This application cache stays in memory;
  * the fixture makes no cache serialization calls. */
 first_pipeline=pipeline;pipeline=VK_NULL_HANDLE;
 XE_COMPILE_BEGIN();OK(g.vkCreateComputePipelines(g.device,pipeline_cache,1,&pipeline_info,NULL,&pipeline));++pipelines;
 XE_COMPILE_END(XE_PROGRAM_STAGE("compute_pso_warm"),0);
#endif
 xe_spv_binary_close(&code);
 pool_info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pool_info.maxSets=1;pool_info.poolSizeCount=XE_PROGRAM_BINDINGS==5?2:1;pool_info.pPoolSizes=pool_size;OK(g.vkCreateDescriptorPool(g.device,&pool_info,NULL,&pool));
 set_allocate.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;set_allocate.descriptorPool=pool;set_allocate.descriptorSetCount=1;set_allocate.pSetLayouts=&set_layout;OK(g.vkAllocateDescriptorSets(g.device,&set_allocate,&set));
 command_pool_info.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;command_pool_info.queueFamilyIndex=family;OK(g.vkCreateCommandPool(g.device,&command_pool_info,NULL,&command_pool));
 command_allocate.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;command_allocate.commandPool=command_pool;command_allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;command_allocate.commandBufferCount=1;OK(g.vkAllocateCommandBuffers(g.device,&command_allocate,&command));
#ifdef XE_SHADER_COMPILE_BENCH
 if(gpu_timing){
  OK(g.vkAllocateCommandBuffers(g.device,&command_allocate,&timestamp_command));
  OK(g.vkAllocateCommandBuffers(g.device,&command_allocate,&completion_command));
  gpu_submits[0].sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;gpu_submits[0].commandBufferCount=1;gpu_submits[0].pCommandBuffers=&timestamp_command;
  gpu_submits[0].signalSemaphoreCount=1;gpu_submits[0].pSignalSemaphores=&timestamp_semaphore;
  gpu_submits[2].sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;gpu_submits[2].commandBufferCount=1;gpu_submits[2].pCommandBuffers=&completion_command;
  gpu_submits[2].waitSemaphoreCount=1;gpu_submits[2].pWaitSemaphores=&completion_semaphore;gpu_submits[2].pWaitDstStageMask=&gpu_wait_stage;
 }
#endif
 fence_info.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;OK(g.vkCreateFence(g.device,&fence_info,NULL,&fence));
 in=(uint32_t*)input_buffer.mapped;out=(uint32_t*)output_buffer.mapped;
 programs=(uint32_t*)program_buffer.mapped;constants=(uint32_t*)constant_buffer.mapped;
 for(round=0;round<3+context_replays;++round){
  limit=round==1?RECORDS-17:RECORDS;output_limit=round==2?OUTPUT_RECORDS-23:OUTPUT_RECORDS;
#if XE_EXTRA_CASES
  if(round>=3){limit=output_limit=2;extra_context((uint32_t*)context_buffer.mapped,round-3);}
#endif
  in[0]=limit;
  for(i=0;i<(round<3?(unsigned)(RECORDS+1)/2:1u);++i){
   round_program(programs+i*21,i,round);
   if(round_swap(round,i))for(lane=0;lane<21;++lane)programs[i*21+lane]=reverse(programs[i*21+lane]);
  }
  if(round<3){
  for(i=0;i<512*4;++i)constants[i]=next(&seed);
  for(lane=0;lane<4;++lane){constants[lane]=0x3f800000u;constants[4+lane]=0x40000000u;constants[8+lane]=0xc0800000u;constants[12+lane]=0;}
  constants[257*4]=0x3f800000u;constants[257*4+1]=0xc0000000u;constants[257*4+2]=0x40400000u;constants[257*4+3]=0xc0800000u;
  constants[511*4]=0x40000000u;constants[511*4+1]=0x40400000u;constants[511*4+2]=0x40800000u;constants[511*4+3]=0x40a00000u;
  }
#if XE_LAZY_CASES
  if(round>=3+XE_LAZY_FIRST)for(lane=0;lane<4;++lane)constants[64*4+lane]=0x3f800000u;
#endif
  for(i=0;i<(round<3?(unsigned)RECORDS:2u);++i){
   group=i/2;kind=group%32;descriptor=in+1+i*8;temporary=in+1+RECORDS*8+i*256;
   descriptor[0]=group*21;descriptor[1]=21u|(round_swap(round,group)?0x80000000u:0);descriptor[2]=1+RECORDS*8+i*256;descriptor[3]=kind==0?0:kind==1?1:64;
   descriptor[4]=0;descriptor[5]=kind==10?0:kind==9?1:512;descriptor[6]=kind==12?511:kind==13?257:0;descriptor[7]=kind==13?0:511;
   for(lane=0;lane<256;++lane){
#if XE_LOOP_CASES
    if(round>=3+XE_BOOL_CASES){float initial=(float)(lane/4)+0.25f;memcpy(temporary+lane,&initial,4);}
    else
#endif
    temporary[lane]=next(&seed);
   }
#if XE_PRED_CASES
   if(round>=3+XE_BOOL_CASES+XE_LOOP_CASES && round<3+XE_COMPARE_FIRST)pred_temporary(temporary,pred_case(round-3-XE_BOOL_CASES-XE_LOOP_CASES,i));
#endif
#if XE_COMPARE_CASES
   if(round>=3+XE_COMPARE_FIRST && round<3+XE_LAZY_FIRST)compare_temporary(temporary,round-3-XE_COMPARE_FIRST);
#endif
#if XE_LAZY_CASES
   if(round>=3+XE_LAZY_FIRST && round<3+XE_REGISTER_FIRST)lazy_temporary(temporary);
#endif
   if(kind==4)for(lane=0;lane<4;++lane)temporary[lane]=0xc0e00000u;
   if(group==5){temporary[0]=0xbf800000u;temporary[1]=0x40000000u;temporary[2]=0xc0400000u;temporary[3]=0x40800000u;}
   if(group==8){static const uint32_t initial[8]={0x3f800000u,0x40000000u,0x40400000u,0x40800000u,0x40a00000u,0x40c00000u,0x40e00000u,0x41000000u};memcpy(temporary,initial,sizeof(initial));}
   if(round>=3){descriptor[3]=64;descriptor[5]=512;descriptor[6]=0;descriptor[7]=511;}
#if XE_MINMAX_CASES
   if(round>=3+XE_MINMAX_FIRST && round<3+XE_DOT_FIRST)minmax_temporary(temporary,round-3-XE_MINMAX_FIRST);
#endif
#if XE_DOT_CASES
   if(round>=3+XE_DOT_FIRST)dot_temporary(temporary,round-3-XE_DOT_FIRST);
#endif
#if XE_REGISTER_CASES
   if(round>=3+XE_REGISTER_FIRST && round<3+XE_MINMAX_FIRST){register_temporary(temporary);descriptor[3]=register_count(round-3-XE_REGISTER_FIRST);}
#endif
   if(kind==16)descriptor[0]=UINT32_MAX;
   if(kind==17)descriptor[1]=2;
   if(kind==18)descriptor[3]=65;
   if(kind==19)descriptor[5]=513;
   if(kind==20)descriptor[4]=UINT32_MAX;
   if(kind==21)descriptor[2]=UINT32_MAX;
   /* Companion timing profile: retain every successful record unchanged, but
    * reject inactive negative records before their temporary loads/CF budget.
    * No program/sample payload is compacted or copied. Full mode is unchanged. */
   if(valid_work && expected_status(kind)!=1)descriptor[1]=0;
  }
  memset(out,0x6d,OUTPUT_RECORDS*32);
  CHECK(cache_buffer(&input_buffer,0) && cache_buffer(&program_buffer,0) && cache_buffer(&constant_buffer,0) && cache_buffer(&output_buffer,0));
#ifdef XE_ALU_CONTROL_TEST
  CHECK(cache_buffer(&context_buffer,0));
#endif
  memset(infos,0,sizeof(infos));infos[0].buffer=input_buffer.buffer;infos[0].range=INPUT_WORDS*4;
  infos[1].buffer=program_buffer.buffer;infos[1].range=PROGRAM_WORDS*4;
  infos[2].buffer=constant_buffer.buffer;infos[2].range=512*16;
  infos[3].buffer=output_buffer.buffer;infos[3].range=output_limit*32;
#ifdef XE_ALU_CONTROL_TEST
  infos[4].buffer=context_buffer.buffer;infos[4].range=160;
#endif
  memset(writes,0,sizeof(writes));for(i=0;i<XE_PROGRAM_BINDINGS;++i){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=set;writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=bindings[i].descriptorType;writes[i].pBufferInfo=&infos[i];}
  g.vkUpdateDescriptorSets(g.device,XE_PROGRAM_BINDINGS,writes,0,NULL);OK(g.vkResetCommandPool(g.device,command_pool,0));
  begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
#ifdef XE_SHADER_COMPILE_BENCH
  if(gpu_timing && round<3){
   /* GPU dependencies enclose the whole workload, not just a pipeline stage.
    * Three batches use one queue-submit call and the existing final fence. */
   OK(g.vkBeginCommandBuffer(timestamp_command,&begin));g.vkCmdResetQueryPool(timestamp_command,query_pool,0,2);
   g.vkCmdWriteTimestamp(timestamp_command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,query_pool,0);
   OK(g.vkEndCommandBuffer(timestamp_command));
   if(gpu_record_delay){double started=xe_compile_now();while(xe_compile_now()-started<.025){}}
   OK(g.vkBeginCommandBuffer(completion_command,&begin));
   g.vkCmdWriteTimestamp(completion_command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,query_pool,1);
   OK(g.vkEndCommandBuffer(completion_command));
  }
#endif
  OK(g.vkBeginCommandBuffer(command,&begin));
  barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_UNIFORM_READ_BIT;
  g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,NULL,0,NULL);
  g.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);g.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);
  {unsigned repetition,repetitions=1;
#ifdef XE_SHADER_COMPILE_BENCH
   VkMemoryBarrier repeated_write={0};
   if(gpu_timing && round<3)repetitions=gpu_repetitions;
   repeated_write.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;repeated_write.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;repeated_write.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
#endif
   for(repetition=0;repetition<repetitions;++repetition){
    g.vkCmdDispatch(command,(OUTPUT_RECORDS+63)/64+1,1,1);
#ifdef XE_SHADER_COMPILE_BENCH
    if(gpu_timing && round<3){++gpu_dispatches;
     if(repetition+1<repetitions)g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&repeated_write,0,NULL,0,NULL);
    }
#endif
   }
  }
  barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;g.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
  OK(g.vkEndCommandBuffer(command));OK(g.vkResetFences(g.device,1,&fence));submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&command;
#ifdef XE_SHADER_COMPILE_BENCH
  if(gpu_timing && round<3){
   gpu_submits[1]=submit;gpu_submits[1].waitSemaphoreCount=1;gpu_submits[1].pWaitSemaphores=&timestamp_semaphore;gpu_submits[1].pWaitDstStageMask=&gpu_wait_stage;
   gpu_submits[1].signalSemaphoreCount=1;gpu_submits[1].pSignalSemaphores=&completion_semaphore;
  }
#endif
  if(round<3){XE_COMPILE_BEGIN();}
#ifdef XE_SHADER_COMPILE_BENCH
  if(gpu_timing && round<3){gpu_started=xe_compile_now();OK(g.vkQueueSubmit(queue,3,gpu_submits,fence));}
  else
#endif
  {OK(g.vkQueueSubmit(queue,1,&submit,fence));}
  completion=g.vkWaitForFences(g.device,1,&fence,VK_TRUE,UINT64_C(5000000000));
  if(completion!=VK_SUCCESS){fprintf(stderr,"GPU completion failed (%d); retaining pending test resources\n",(int)completion);exit(1);}
#ifdef XE_SHADER_COMPILE_BENCH
  if(gpu_timing && round<3)gpu_host_seconds=xe_compile_now()-gpu_started;
#endif
 XE_COMPILE_END(XE_PROGRAM_STAGE("submit_complete"),round);
#ifdef XE_SHADER_COMPILE_BENCH
  if(gpu_timing && round<3){
   memset(gpu_ticks,0,sizeof(gpu_ticks));
   OK(g.vkGetQueryPoolResults(g.device,query_pool,0,2,sizeof(gpu_ticks),gpu_ticks,2*sizeof(uint64_t),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
   CHECK(xe_test_gpu_timestamp_seconds(gpu_ticks,families[family].timestampValidBits,properties.limits.timestampPeriod,gpu_host_seconds,&gpu_seconds));
   CHECK(gpu_timing_record(gpu_path,gpu_repetitions==1?XE_PROGRAM_STAGE("gpu_dispatch"):XE_PROGRAM_STAGE("gpu_dispatch_repeat8"),round,gpu_ticks,families[family].timestampValidBits,properties.limits.timestampPeriod,gpu_host_seconds,gpu_seconds));
  }
#endif
  CHECK(cache_buffer(&output_buffer,1) && cache_buffer(&program_buffer,1));
#if XE_EXTRA_CASES
  if(round>=3){uint32_t expected_bank[40];extra_context(expected_bank,round-3);
   CHECK(cache_buffer(&context_buffer,1) && !memcmp(context_buffer.mapped,expected_bank,160));}
#endif
  for(i=0;i<(round<3?(unsigned)(RECORDS+1)/2:1u);++i){
   round_program(reference_code,i,round);
   for(lane=0;lane<21;++lane)CHECK(programs[i*21+lane]==
    (round_swap(round,i)?reverse(reference_code[lane]):reference_code[lane]));
  }
  for(i=0;i<OUTPUT_RECORDS;++i){
   if(i>=limit || i>=output_limit){for(lane=0;lane<8;++lane)CHECK(out[i*8+lane]==0x6d6d6d6du);continue;}
   group=i/2;kind=group%32;status=expected_status(kind);
#if XE_EXTRA_CASES
   if(round>=3)status=extra_status(extra_which(round-3,i));
#endif
   if(valid_work && status!=1){status=2;++inactive;}
   if(out[i*8+4]!=status){fprintf(stderr,"round %u invocation %u kind %u status %u != %u\n",round,i,kind,out[i*8+4],status);goto done;}
   if(status==1){
    descriptor=in+1+i*8;
    round_program(reference_code,group,round);
#if XE_EXTRA_CASES
    if(round>=3){CHECK(xe_alu_control_reference(reference_code,in+descriptor[2],descriptor[3],constants+descriptor[4],descriptor[5],descriptor[6],descriptor[7],reference,(uint32_t*)context_buffer.mapped)==1);}
    else
#endif
    CHECK(xe_alu_program_reference(reference_code,in+descriptor[2],descriptor[3],constants+descriptor[4],descriptor[5],descriptor[6],descriptor[7],reference)==1);
    for(lane=0;lane<7;++lane){if(lane==4)continue;
     if(!same(out[i*8+lane],reference[lane])){fprintf(stderr,"round %u invocation %u kind %u word %u: %08x != %08x\n",round,i,kind,lane,out[i*8+lane],reference[lane]);goto done;}}
#ifndef XE_ALU_CONTROL_TEST
    if(group==0)for(lane=0;lane<4;++lane)CHECK(out[i*8+lane]==0x40400000u);
    if(group==2)for(lane=0;lane<4;++lane)CHECK(out[i*8+lane]==0xc0000000u);
    if(group==4)for(lane=0;lane<4;++lane)CHECK(out[i*8+lane]==0xc1300000u);
    if(group==5){static const uint32_t expected[4]={0,0x40400000u,0xc0000000u,0x40400000u};for(lane=0;lane<4;++lane)CHECK(out[i*8+lane]==expected[lane]);}
    if(group==8){static const uint32_t expected[4]={0x3f800000u,0x40c00000u,0x40400000u,0x41000000u};for(lane=0;lane<4;++lane)CHECK(out[i*8+lane]==expected[lane]);}
    if(group==9)for(lane=0;lane<4;++lane)CHECK(out[i*8+lane]==0x3f800000u);
    if(group==10)for(lane=0;lane<4;++lane)CHECK(out[i*8+lane]==0);
    if(group==11)for(lane=0;lane<4;++lane)CHECK(out[i*8+lane]==0xc1000000u);
#else
#if XE_EXTRA_CASES
    if(round>=3){for(lane=0;lane<4;++lane){uint32_t golden=extra_golden(extra_which(round-3,i),lane);
     if(out[i*8+lane]!=golden){fprintf(stderr,"context round %u invocation %u lane %u: %08x != golden %08x\n",round,i,lane,out[i*8+lane],golden);goto done;}}}
    else
#endif
    CHECK(control_golden(group,out+i*8));
#if XE_LOOP_CASES
    if(round>=3+XE_BOOL_CASES && round<3+XE_BOOL_CASES+XE_LOOP_CASES)CHECK(out[i*8+5]==loop_exports(round-3-XE_BOOL_CASES));
#endif
#if XE_PRED_CASES
    if(round>=3+XE_BOOL_CASES+XE_LOOP_CASES && round<3+XE_COMPARE_FIRST)CHECK(out[i*8+5]==pred_exports(pred_case(round-3-XE_BOOL_CASES-XE_LOOP_CASES,i)));
#endif
#if XE_COMPARE_CASES
    if(round>=3+XE_COMPARE_FIRST && round<3+XE_LAZY_FIRST)CHECK(out[i*8+5]==1);
#endif
#if XE_LAZY_CASES
    if(round>=3+XE_LAZY_FIRST && round<3+XE_REGISTER_FIRST)CHECK(out[i*8+5]==lazy_exports(round-3-XE_LAZY_FIRST));
#endif
#endif
    ++compared;
   }else{for(lane=0;lane<7;++lane)if(lane!=4)CHECK(out[i*8+lane]==0x6d6d6d6du);}
   CHECK(out[i*8+7]==0x6d6d6d6du);++cases;
#if XE_DOT_CASES
   if(round>=3+XE_DOT_FIRST){if(status==1)CHECK(out[i*8+5]==1);++dot_invocations;}
#endif
#if XE_MINMAX_CASES
   if(round>=3+XE_MINMAX_FIRST && round<3+XE_DOT_FIRST){if(status==1)CHECK(out[i*8+5]==1);++minmax_invocations;}
#endif
#if XE_REGISTER_CASES
   if(round>=3+XE_REGISTER_FIRST && round<3+XE_MINMAX_FIRST){CHECK(out[i*8+5]==1);++register_invocations;}
#endif
#if XE_COMPARE_CASES
   if(round>=3+XE_COMPARE_FIRST && round<3+XE_LAZY_FIRST)++comparison_invocations;
#endif
#if XE_LAZY_CASES
   if(round>=3+XE_LAZY_FIRST && round<3+XE_REGISTER_FIRST)++lazy_invocations;
#endif
  }
 }
 CHECK(modules==1 && compared>300);
#if XE_DOT_CASES
 if(context_replays){CHECK(dot_invocations==2*XE_DOT_CASES);
  printf("DOT_CONTEXT replays=%u invocations=%u\n",dot_invocations/2,dot_invocations);}
#else
 (void)dot_invocations;
#endif
#if XE_MINMAX_CASES
 if(context_replays){CHECK(minmax_invocations==2*XE_MINMAX_CASES);
  printf("MINMAX_CONTEXT replays=%u invocations=%u\n",minmax_invocations/2,minmax_invocations);}
#endif
#if XE_REGISTER_CASES
 if(context_replays){CHECK(register_invocations==2*XE_REGISTER_CASES);
  printf("REGISTER_CONTEXT replays=%u invocations=%u\n",register_invocations/2,register_invocations);}
#endif
#if XE_LAZY_CASES
 if(context_replays){CHECK(lazy_invocations==2*XE_LAZY_CASES);
  printf("LAZY_SOURCE_CONTEXT replays=%u invocations=%u\n",lazy_invocations/2,lazy_invocations);}
#endif
#if XE_COMPARE_CASES
 if(context_replays){
  CHECK(comparison_invocations==2*XE_COMPARE_CASES);
  printf("COMPARE_CONTEXT replays=%u invocations=%u\n",comparison_invocations/2,comparison_invocations);
 }
#endif
 if(valid_work){
  CHECK(cases==743 && inactive>0 && compared+inactive==cases && !context_replays);
  printf("%s active=%u inactive=%u; inactive lanes rejected before program execution\n",gpu_timing?"PROFILE_VALID_WORK_GPU":"PROFILE_VALID_WORK",compared,inactive);
 }
#ifdef XE_SHADER_COMPILE_BENCH
 CHECK(pipelines==2);
 if(gpu_timing){CHECK(gpu_dispatches==3*gpu_repetitions);printf("GPU_TIMESTAMP_EXECUTED repetitions=%u dispatches=%u record_delay=%u\n",gpu_repetitions,gpu_dispatches,(unsigned)gpu_record_delay);}
#else
 CHECK(pipelines==1);
#endif
 printf("PASS: %u packed program invocations (%u native CPU-reference comparisons); shared bytecode/constants, transactional failure/bounds; one active module/PSO across three primary and %u additional context replays (%u initialization PSO creates)\n",cases,compared,context_replays,pipelines);result=0;
done:
 if(g.device){if(fence)g.vkDestroyFence(g.device,fence,NULL);
  if(command_pool)g.vkDestroyCommandPool(g.device,command_pool,NULL);
  if(pool)g.vkDestroyDescriptorPool(g.device,pool,NULL);
  if(pipeline)g.vkDestroyPipeline(g.device,pipeline,NULL);
#ifdef XE_SHADER_COMPILE_BENCH
  if(query_pool)g.vkDestroyQueryPool(g.device,query_pool,NULL);
  if(timestamp_semaphore)g.vkDestroySemaphore(g.device,timestamp_semaphore,NULL);
  if(completion_semaphore)g.vkDestroySemaphore(g.device,completion_semaphore,NULL);
  if(first_pipeline)g.vkDestroyPipeline(g.device,first_pipeline,NULL);
  if(pipeline_cache)g.vkDestroyPipelineCache(g.device,pipeline_cache,NULL);
#endif
  if(module)g.vkDestroyShaderModule(g.device,module,NULL);
  if(layout)g.vkDestroyPipelineLayout(g.device,layout,NULL);
  if(set_layout)g.vkDestroyDescriptorSetLayout(g.device,set_layout,NULL);
#ifdef XE_ALU_CONTROL_TEST
  close_buffer(&context_buffer);
#endif
  close_buffer(&output_buffer);close_buffer(&constant_buffer);close_buffer(&program_buffer);close_buffer(&input_buffer);g.vkDestroyDevice(g.device,NULL);}
 if(g.instance)g.vkDestroyInstance(g.instance,NULL);
 xe_spv_binary_close(&code);
#ifdef _WIN32
 if(loader)FreeLibrary(loader);
#else
 if(loader)dlclose(loader);
#endif
 return result;}
