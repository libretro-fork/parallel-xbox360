/* Single-PSO dynamic addressing fixture. Raw SPIR-V1.0; BSD; C89. */
#include "libretro/core_transfer_sample_spv.h"
#include <string.h>
enum {TV=1,TU,TB,T3,TPAIR,TARRAY,TBUFFER,PB,PU,PI,TF,
 INPUT,OUTPUT,INVOCATION,MAIN,LABEL,WORK,END,CONST_BASE=32};
typedef struct fixture {
 xe_spv_binary *code;uint32_t next;
} fixture;
static void put(fixture *f,unsigned op,uint32_t type,uint32_t result,
 unsigned count,uint32_t a,uint32_t b,uint32_t c) {
 uint32_t words[3];words[0]=a;words[1]=b;words[2]=c;
 xe_spv_binary_instruction(f->code,op,type,result,words,count);
}
static uint32_t constant(unsigned n){return CONST_BASE+n;}
static uint32_t bin(fixture *f,unsigned op,uint32_t type,uint32_t a,uint32_t b) {
 uint32_t id=f->next++;put(f,op,type,id,2,a,b,0);return id;
}
static uint32_t load(fixture *f,uint32_t base,unsigned field) {
 uint32_t index=bin(f,SpvOpIAdd,TU,base,constant(field)),pointer=f->next++,value=f->next++;
 put(f,SpvOpAccessChain,PU,pointer,3,INPUT,constant(0),index);
 put(f,SpvOpLoad,TU,value,1,pointer,0,0);return value;
}
static void store(fixture *f,uint32_t base,unsigned field,uintptr_t value) {
 uint32_t index=bin(f,SpvOpIAdd,TU,base,constant(field)),pointer=f->next++;
 put(f,SpvOpAccessChain,PU,pointer,3,OUTPUT,constant(0),index);
 put(f,SpvOpStore,0,0,2,pointer,(uint32_t)value,0);
}
int xe_test_transfer_sample_spv(xe_spv_binary *out) {
 fixture f;xe_transfer_sample_spv adapter;xe_transfer_sample_emitter emitter;
 xe_transfer_sample_parameters p;xe_transfer_sample_coordinates canonical,inverse,result;
 uint32_t header[5]={0x07230203u,0x00010000u,0,0,0};
 uint32_t entry[5]={SpvExecutionModelGLCompute,MAIN,0x6e69616du,0,INVOCATION};
 uint32_t mode[5]={MAIN,SpvExecutionModeLocalSize,64,1,1};
 uint32_t invocation,index,in_length,out_length,in_count,out_count,a,b,c,valid,base,dest;
 unsigned i;
 if(!out)return 0;
 xe_spv_binary_reset(out);if(!xe_spv_binary_reserve(out,2048))return 0;
 f.code=out;f.next=128;xe_spv_binary_append(out,header,5);
 put(&f,SpvOpCapability,0,0,1,SpvCapabilityShader,0,0);
 put(&f,SpvOpMemoryModel,0,0,2,SpvAddressingModelLogical,SpvMemoryModelGLSL450,0);
 xe_spv_binary_instruction(out,SpvOpEntryPoint,0,0,entry,5);
 xe_spv_binary_instruction(out,SpvOpExecutionMode,0,0,mode,5);
 put(&f,SpvOpDecorate,0,0,3,INVOCATION,SpvDecorationBuiltIn,SpvBuiltInGlobalInvocationId);
 put(&f,SpvOpDecorate,0,0,3,TARRAY,SpvDecorationArrayStride,4);
 put(&f,SpvOpDecorate,0,0,2,TBUFFER,SpvDecorationBufferBlock,0);
 {uint32_t member[4]={TBUFFER,0,SpvDecorationOffset,0};
  xe_spv_binary_instruction(out,SpvOpMemberDecorate,0,0,member,4);}
 for(i=0;i<2;++i) {
  put(&f,SpvOpDecorate,0,0,3,INPUT+i,SpvDecorationDescriptorSet,0);
  put(&f,SpvOpDecorate,0,0,3,INPUT+i,SpvDecorationBinding,i);
 }
 put(&f,SpvOpTypeVoid,0,TV,0,0,0,0);
 put(&f,SpvOpTypeInt,0,TU,2,32,0,0);
 put(&f,SpvOpTypeBool,0,TB,0,0,0,0);
 put(&f,SpvOpTypeVector,0,T3,2,TU,3,0);
 put(&f,SpvOpTypeStruct,0,TPAIR,2,TU,TU,0);
 put(&f,SpvOpTypeRuntimeArray,0,TARRAY,1,TU,0,0);
 put(&f,SpvOpTypeStruct,0,TBUFFER,1,TARRAY,0,0);
 put(&f,SpvOpTypePointer,0,PB,2,SpvStorageClassUniform,TBUFFER,0);
 put(&f,SpvOpTypePointer,0,PU,2,SpvStorageClassUniform,TU,0);
 put(&f,SpvOpTypePointer,0,PI,2,SpvStorageClassInput,T3,0);
 put(&f,SpvOpTypeFunction,0,TF,1,TV,0,0);
 for(i=0;i<=32;++i)put(&f,SpvOpConstant,TU,constant(i),1,i,0,0);
 put(&f,SpvOpConstant,TU,constant(33),1,0xfffffffeu,0,0);
 put(&f,SpvOpConstant,TU,constant(34),1,0xfffffffdu,0,0);
 put(&f,SpvOpConstant,TU,constant(35),1,0xfffffffcu,0,0);
 put(&f,SpvOpConstant,TU,constant(36),1,0xffffffffu,0,0);
 put(&f,SpvOpVariable,PB,INPUT,1,SpvStorageClassUniform,0,0);
 put(&f,SpvOpVariable,PB,OUTPUT,1,SpvStorageClassUniform,0,0);
 put(&f,SpvOpVariable,PI,INVOCATION,1,SpvStorageClassInput,0,0);
 put(&f,SpvOpFunction,TV,MAIN,2,0,TF,0);put(&f,SpvOpLabel,0,LABEL,0,0,0,0);
 invocation=f.next++;put(&f,SpvOpLoad,T3,invocation,1,INVOCATION,0,0);
 index=f.next++;put(&f,SpvOpCompositeExtract,TU,index,2,invocation,0,0);
 in_length=f.next++;out_length=f.next++;
 put(&f,SpvOpArrayLength,TU,in_length,2,INPUT,0,0);
 put(&f,SpvOpArrayLength,TU,out_length,2,OUTPUT,0,0);
 in_count=bin(&f,SpvOpUDiv,TU,in_length,constant(32));
 out_count=bin(&f,SpvOpUDiv,TU,out_length,constant(16));
 a=bin(&f,SpvOpULessThan,TB,index,in_count);b=bin(&f,SpvOpULessThan,TB,index,out_count);
 valid=bin(&f,SpvOpLogicalAnd,TB,a,b);
 put(&f,SpvOpSelectionMerge,0,0,2,END,0,0);
 put(&f,SpvOpBranchConditional,0,0,3,valid,WORK,END);put(&f,SpvOpLabel,0,WORK,0,0,0,0);
 base=bin(&f,SpvOpIMul,TU,index,constant(32));
 dest=bin(&f,SpvOpIMul,TU,index,constant(16));
 p.msaa=load(&f,base,3);p.native2=load(&f,base,4);
 p.scale_x=load(&f,base,5);p.scale_y=load(&f,base,6);
 p.reciprocal_x=load(&f,base,7);p.reciprocal_y=load(&f,base,8);
 memset(&adapter,0,sizeof(adapter));adapter.out=out;adapter.next=&f.next;
 adapter.uint_type=TU;adapter.bool_type=TB;adapter.pair_type=TPAIR;
 adapter.zero=constant(0);adapter.one=constant(1);adapter.two=constant(2);
 adapter.not_one=constant(33);adapter.not_two=constant(34);adapter.not_three=constant(35);
 adapter.all=constant(36);
 emitter.emit=xe_transfer_sample_spv_emit;emitter.context=&adapter;
 a=load(&f,base,0);b=load(&f,base,1);c=load(&f,base,2);
 if(!xe_transfer_sample_canonical_emit(&emitter,&p,a,b,c,&canonical))goto fail;
 inverse.x=load(&f,base,9);inverse.y=load(&f,base,10);
 inverse.sub_x=load(&f,base,11);inverse.sub_y=load(&f,base,12);inverse.sample=constant(0);
 p.msaa=load(&f,base,13);p.native2=load(&f,base,14);
 p.scale_x=load(&f,base,15);p.scale_y=load(&f,base,16);
 if(!xe_transfer_sample_decanonical_emit(&emitter,&p,&inverse,&result))goto fail;
 store(&f,dest,0,canonical.x);store(&f,dest,1,canonical.y);
 store(&f,dest,2,canonical.sub_x);store(&f,dest,3,canonical.sub_y);
 store(&f,dest,4,result.x);store(&f,dest,5,result.y);store(&f,dest,6,result.sample);
 put(&f,SpvOpBranch,0,0,1,END,0,0);put(&f,SpvOpLabel,0,END,0,0,0,0);
 put(&f,SpvOpReturn,0,0,0,0,0,0);put(&f,SpvOpFunctionEnd,0,0,0,0,0,0);
 if(out->failed)goto fail;
 out->words[3]=f.next;return 1;
fail:out->count=0;return 0;
}
#ifdef XE_TRANSFER_SAMPLE_GENERATOR
#include <stdio.h>
int main(int argc,char **argv) {
 xe_spv_binary code={0};FILE *file;int failed;
 if(argc!=2 || !xe_test_transfer_sample_spv(&code))return 1;
 file=fopen(argv[1],"wb");if(!file){xe_spv_binary_close(&code);return 1;}
 failed=fwrite(code.words,4,code.count,file)!=code.count;
 if(fclose(file))failed=1;
 printf("Transfer sample module: %lu words\n",(unsigned long)code.count);
 xe_spv_binary_close(&code);return failed;
}
#endif
