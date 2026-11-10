/* Independent MS image fill/readback shaders; test-only raw SPIR-V, BSD C89. */
#include "libretro/core_spv_binary.h"
#include <spirv/unified1/spirv.h>
#include <stdio.h>
#include <string.h>
enum {V=1,U,B,F,U2,U3,U4,F4,ARRAY,BUFFER,IMAGE_TYPE,PC_ARRAY,PC,
 PBUFFER,PWORD,PIMAGE,PPC,PPCWORD,PINPUT,POUTPUT,FUNC,
 INPUT,OUTPUT,PUSH,IMAGE,MAIN,LABEL,WORK,END,CBASE=64};
typedef struct code {xe_spv_binary *out;uint32_t next;} code;
static uint32_t c(unsigned n){return CBASE+n;}
static void put(code *s,unsigned op,uint32_t type,uint32_t id,
 unsigned n,uint32_t a,uint32_t b,uint32_t d) {
 uint32_t words[3];words[0]=a;words[1]=b;words[2]=d;
 xe_spv_binary_instruction(s->out,op,type,id,words,n);
}
static uint32_t val(code *s,unsigned op,uint32_t type,unsigned n,
 uint32_t a,uint32_t b,uint32_t d) {
 uint32_t id=s->next++;put(s,op,type,id,n,a,b,d);return id;
}
static uint32_t field(code *s,unsigned n) {
 uint32_t p=val(s,SpvOpAccessChain,PPCWORD,3,PUSH,c(0),c(n));
 return val(s,SpvOpLoad,U,1,p,0,0);
}
static uint32_t shift(code *s,uint32_t a,unsigned n){return val(s,SpvOpShiftLeftLogical,U,2,a,c(n),0);}
static uint32_t xor2(code *s,uint32_t a,uint32_t b){return val(s,SpvOpBitwiseXor,U,2,a,b,0);}
static void setup(code *s,int probe,unsigned ms) {
 uint32_t header[5]={0x07230203u,0x00010000u,0,0,0};
 uint32_t entry[6]={SpvExecutionModelFragment,MAIN,0x6e69616du,0,INPUT,OUTPUT};
 uint32_t member[4]={PC,0,SpvDecorationOffset,0};
 uint32_t image[7]={U,SpvDim2D,0,0,0,1,SpvImageFormatUnknown};
 uint32_t mode[5]={MAIN,SpvExecutionModeLocalSize,64,1,1};unsigned i;
 xe_spv_binary_append(s->out,header,5);
 put(s,SpvOpCapability,0,0,1,SpvCapabilityShader,0,0);
 put(s,SpvOpMemoryModel,0,0,2,SpvAddressingModelLogical,SpvMemoryModelGLSL450,0);
 if(probe)entry[0]=SpvExecutionModelGLCompute;
 xe_spv_binary_instruction(s->out,SpvOpEntryPoint,0,0,entry,probe?5:6);
 if(probe)xe_spv_binary_instruction(s->out,SpvOpExecutionMode,0,0,mode,5);
 else put(s,SpvOpExecutionMode,0,0,2,MAIN,SpvExecutionModeOriginUpperLeft,0);
 put(s,SpvOpDecorate,0,0,3,INPUT,SpvDecorationBuiltIn,probe?SpvBuiltInGlobalInvocationId:SpvBuiltInFragCoord);
 if(!probe)put(s,SpvOpDecorate,0,0,3,OUTPUT,SpvDecorationLocation,0);
 put(s,SpvOpDecorate,0,0,3,PC_ARRAY,SpvDecorationArrayStride,4);
 put(s,SpvOpDecorate,0,0,2,PC,SpvDecorationBlock,0);
 xe_spv_binary_instruction(s->out,SpvOpMemberDecorate,0,0,member,4);
 if(probe) {
  put(s,SpvOpDecorate,0,0,3,ARRAY,SpvDecorationArrayStride,4);
  put(s,SpvOpDecorate,0,0,2,BUFFER,SpvDecorationBufferBlock,0);
  member[0]=BUFFER;xe_spv_binary_instruction(s->out,SpvOpMemberDecorate,0,0,member,4);
  put(s,SpvOpDecorate,0,0,3,IMAGE,SpvDecorationDescriptorSet,0);
  put(s,SpvOpDecorate,0,0,3,IMAGE,SpvDecorationBinding,0);
  put(s,SpvOpDecorate,0,0,3,OUTPUT,SpvDecorationDescriptorSet,0);
  put(s,SpvOpDecorate,0,0,3,OUTPUT,SpvDecorationBinding,1);
 }
 put(s,SpvOpTypeVoid,0,V,0,0,0,0);put(s,SpvOpTypeInt,0,U,2,32,0,0);
 put(s,SpvOpTypeBool,0,B,0,0,0,0);put(s,SpvOpTypeFloat,0,F,1,32,0,0);
 put(s,SpvOpTypeVector,0,U2,2,U,2,0);put(s,SpvOpTypeVector,0,U3,2,U,3,0);
 put(s,SpvOpTypeVector,0,U4,2,U,4,0);put(s,SpvOpTypeVector,0,F4,2,F,4,0);
 for(i=0;i<=32;++i)put(s,SpvOpConstant,U,c(i),1,i,0,0);
 put(s,SpvOpConstant,U,c(33),1,0x7fc00000u,0,0);
 put(s,SpvOpConstant,U,c(34),1,0x80008000u,0,0);
 put(s,SpvOpConstant,U,c(35),1,0xffff0000u,0,0);
 put(s,SpvOpConstant,U,c(36),1,0x0000ffffu,0,0);
 put(s,SpvOpTypeArray,0,PC_ARRAY,2,U,c(4),0);
 put(s,SpvOpTypeStruct,0,PC,1,PC_ARRAY,0,0);
 put(s,SpvOpTypePointer,0,PPC,2,SpvStorageClassPushConstant,PC,0);
 put(s,SpvOpTypePointer,0,PPCWORD,2,SpvStorageClassPushConstant,U,0);
 put(s,SpvOpTypePointer,0,PINPUT,2,SpvStorageClassInput,probe?U3:F4,0);
 if(probe) {
  image[4]=ms;xe_spv_binary_instruction(s->out,SpvOpTypeImage,0,IMAGE_TYPE,image,7);
  put(s,SpvOpTypeRuntimeArray,0,ARRAY,1,U,0,0);put(s,SpvOpTypeStruct,0,BUFFER,1,ARRAY,0,0);
  put(s,SpvOpTypePointer,0,PBUFFER,2,SpvStorageClassUniform,BUFFER,0);
  put(s,SpvOpTypePointer,0,PWORD,2,SpvStorageClassUniform,U,0);
  put(s,SpvOpTypePointer,0,PIMAGE,2,SpvStorageClassUniformConstant,IMAGE_TYPE,0);
  put(s,SpvOpVariable,PBUFFER,OUTPUT,1,SpvStorageClassUniform,0,0);
  put(s,SpvOpVariable,PIMAGE,IMAGE,1,SpvStorageClassUniformConstant,0,0);
 } else {
  put(s,SpvOpTypePointer,0,POUTPUT,2,SpvStorageClassOutput,U4,0);
  put(s,SpvOpVariable,POUTPUT,OUTPUT,1,SpvStorageClassOutput,0,0);
 }
 put(s,SpvOpTypeFunction,0,FUNC,1,V,0,0);
 put(s,SpvOpVariable,PPC,PUSH,1,SpvStorageClassPushConstant,0,0);
 put(s,SpvOpVariable,PINPUT,INPUT,1,SpvStorageClassInput,0,0);
 put(s,SpvOpFunction,V,MAIN,2,0,FUNC,0);put(s,SpvOpLabel,0,LABEL,0,0,0,0);
}
static int build(xe_spv_binary *out,int probe,unsigned ms) {
 code s;uint32_t x,y,sample,input,width,height,total,index,length,a,b,base,pixel,coordinates,image,value,pointer,args[4];unsigned i;
 xe_spv_binary_reset(out);if(!xe_spv_binary_reserve(out,1024))return 0;
 s.out=out;s.next=128;setup(&s,probe,ms);
 input=val(&s,SpvOpLoad,probe?U3:F4,1,INPUT,0,0);
 if(!probe) {
  x=val(&s,SpvOpCompositeExtract,F,2,input,0,0);x=val(&s,SpvOpConvertFToU,U,1,x,0,0);
  y=val(&s,SpvOpCompositeExtract,F,2,input,1,0);y=val(&s,SpvOpConvertFToU,U,1,y,0,0);sample=field(&s,0);
  args[0]=xor2(&s,c(33),xor2(&s,x,xor2(&s,shift(&s,y,8),shift(&s,sample,20))));
  args[1]=xor2(&s,c(34),xor2(&s,shift(&s,x,4),xor2(&s,y,shift(&s,sample,12))));
  args[2]=xor2(&s,c(35),xor2(&s,shift(&s,x,8),xor2(&s,shift(&s,y,1),shift(&s,sample,14))));
  args[3]=xor2(&s,c(36),xor2(&s,x,xor2(&s,shift(&s,y,8),shift(&s,sample,4))));
  /* Integer attachment conversion saturates, it does not truncate low bits. */
  a=field(&s,1);for(i=0;i<4;++i)args[i]=val(&s,SpvOpBitwiseAnd,U,2,args[i],a,0);
  value=s.next++;xe_spv_binary_instruction(out,SpvOpCompositeConstruct,U4,value,args,4);
  put(&s,SpvOpStore,0,0,2,OUTPUT,value,0);
 } else {
  index=val(&s,SpvOpCompositeExtract,U,2,input,0,0);width=field(&s,0);height=field(&s,1);sample=field(&s,2);
  total=val(&s,SpvOpIMul,U,2,width,height,0);total=val(&s,SpvOpIMul,U,2,total,sample,0);
  length=val(&s,SpvOpArrayLength,U,2,OUTPUT,0,0);length=val(&s,SpvOpUDiv,U,2,length,c(4),0);
  a=val(&s,SpvOpULessThan,B,2,index,total,0);b=val(&s,SpvOpULessThan,B,2,index,length,0);
  a=val(&s,SpvOpLogicalAnd,B,2,a,b,0);put(&s,SpvOpSelectionMerge,0,0,2,END,0,0);
  put(&s,SpvOpBranchConditional,0,0,3,a,WORK,END);put(&s,SpvOpLabel,0,WORK,0,0,0,0);
  pixel=val(&s,SpvOpUDiv,U,2,index,sample,0);sample=val(&s,SpvOpUMod,U,2,index,sample,0);
  x=val(&s,SpvOpUMod,U,2,pixel,width,0);y=val(&s,SpvOpUDiv,U,2,pixel,width,0);
  coordinates=val(&s,SpvOpCompositeConstruct,U2,2,x,y,0);image=val(&s,SpvOpLoad,IMAGE_TYPE,1,IMAGE,0,0);
  args[0]=image;args[1]=coordinates;args[2]=ms?SpvImageOperandsSampleMask:SpvImageOperandsLodMask;args[3]=ms?sample:c(0);
  value=s.next++;xe_spv_binary_instruction(out,SpvOpImageFetch,U4,value,args,4);
  base=val(&s,SpvOpIMul,U,2,index,c(4),0);
  for(i=0;i<4;++i){a=val(&s,SpvOpIAdd,U,2,base,c(i),0);pointer=val(&s,SpvOpAccessChain,PWORD,3,OUTPUT,c(0),a);a=val(&s,SpvOpCompositeExtract,U,2,value,i,0);put(&s,SpvOpStore,0,0,2,pointer,a,0);}
  put(&s,SpvOpBranch,0,0,1,END,0,0);put(&s,SpvOpLabel,0,END,0,0,0,0);
 }
 put(&s,SpvOpReturn,0,0,0,0,0,0);put(&s,SpvOpFunctionEnd,0,0,0,0,0,0);
 if(out->failed){out->count=0;return 0;}out->words[3]=s.next;return 1;
}
int main(int argc,char **argv) {
 xe_spv_binary out={0};FILE *f;char path[4096];unsigned i;int failed;
 if(argc!=2 || strlen(argv[1])>2048)return 1;
 for(i=0;i<3;++i){if(!build(&out,i!=0,i==2))goto fail;sprintf(path,"%s-%s.spv",argv[1],i==0?"fill":i==1?"probe-s0":"probe-s1");f=fopen(path,"wb");if(!f)goto fail;failed=fwrite(out.words,4,out.count,f)!=out.count;if(fclose(f))failed=1;if(failed)goto fail;}
 xe_spv_binary_close(&out);return 0;
fail:xe_spv_binary_close(&out);return 1;
}
