/* Initialization-only raw SPIR-V fragment construction. BSD; MSVC C89. */
#include "core_transfer_uint_spv.h"
#include "core_transfer_sample_spv.h"
#include <string.h>

enum {
 TV=1,TU,TB,TF,TU2,TU4,TF4,TPAIR,TIMAGE,TPCARRAY,TPC,
 PIMAGE,PPC,PPU,POUT,PPOS,PSAMPLE,TFUNC,
 IMAGE,CONSTANTS,OUTPUT,POSITION,SAMPLE,MAIN,LABEL,FETCH2,MERGE,
 CONST_BASE=40,FIRST_ID=128
};
typedef struct uint_shader {xe_spv_binary *out;uint32_t next;} uint_shader;
static uint32_t literal(unsigned n){return CONST_BASE+n;}
static void instruction(uint_shader *s,unsigned op,uint32_t type,uint32_t id,
 unsigned count,uint32_t a,uint32_t b,uint32_t c) {
 uint32_t words[3];words[0]=a;words[1]=b;words[2]=c;
 xe_spv_binary_instruction(s->out,op,type,id,words,count);
}
static uint32_t value(uint_shader *s,unsigned op,uint32_t type,
 unsigned count,uint32_t a,uint32_t b,uint32_t c) {
 uint32_t id=s->next++;instruction(s,op,type,id,count,a,b,c);return id;
}
static uint32_t parameter(uint_shader *s,unsigned word) {
 uint32_t pointer=value(s,SpvOpAccessChain,PPU,3,CONSTANTS,literal(0),literal(word));
 return value(s,SpvOpLoad,TU,1,pointer,0,0);
}
static int fetch(uint_shader *s,uint32_t image,uint32_t x,uint32_t y,uint32_t sample,
 unsigned multisampled,uintptr_t lanes[4]) {
 uint32_t coordinates=value(s,SpvOpCompositeConstruct,TU2,2,x,y,0);
 uint32_t result=s->next++,args[4];unsigned i;
 args[0]=image;args[1]=coordinates;
 args[2]=multisampled?SpvImageOperandsSampleMask:SpvImageOperandsLodMask;
 args[3]=multisampled?sample:literal(0);
 xe_spv_binary_instruction(s->out,SpvOpImageFetch,TU4,result,args,4);
 for(i=0;i<4;++i)lanes[i]=value(s,SpvOpCompositeExtract,TU,2,result,i,0);
 return !s->out->failed;
}
int xe_transfer_uint_spv_build(xe_spv_binary *out,
 const xe_transfer_uint_options *options) {
 uint_shader s;xe_transfer_sample_spv adapter;xe_transfer_sample_emitter e;
 xe_transfer_address_parameters address;xe_transfer_address_result route;
 xe_transfer_uint_color_parameters source,dest;
 uintptr_t lanes[4],first[2],second[2],packed[2],result[4];
 uint32_t header[5]={0x07230203u,0x00010000u,0,0,0};
 uint32_t entry[8]={SpvExecutionModelFragment,MAIN,0x6e69616du,0,POSITION,OUTPUT,SAMPLE,0};
 uint32_t image_type[7]={TU,SpvDim2D,0,0,0,1,SpvImageFormatUnknown};
 uint32_t member[4]={TPC,0,SpvDecorationOffset,0};
 uint32_t position,x,y,sample,image,condition,second_word,args[4];unsigned i;
 if(!out || !options || options->source_multisampled>1 ||
    options->sample_mode>XE_TRANSFER_UINT_SAMPLE_CONSTANT || options->target>3)return 0;
 xe_spv_binary_reset(out);if(!xe_spv_binary_reserve(out,4096))return 0;
 s.out=out;s.next=FIRST_ID;xe_spv_binary_append(out,header,5);
 instruction(&s,SpvOpCapability,0,0,1,SpvCapabilityShader,0,0);
 if(options->sample_mode==XE_TRANSFER_UINT_SAMPLE_BUILTIN)
  instruction(&s,SpvOpCapability,0,0,1,SpvCapabilitySampleRateShading,0,0);
 instruction(&s,SpvOpMemoryModel,0,0,2,SpvAddressingModelLogical,SpvMemoryModelGLSL450,0);
 xe_spv_binary_instruction(out,SpvOpEntryPoint,0,0,entry,
  options->sample_mode==XE_TRANSFER_UINT_SAMPLE_BUILTIN?7:6);
 instruction(&s,SpvOpExecutionMode,0,0,2,MAIN,SpvExecutionModeOriginUpperLeft,0);
 instruction(&s,SpvOpDecorate,0,0,3,POSITION,SpvDecorationBuiltIn,SpvBuiltInFragCoord);
 if(options->sample_mode==XE_TRANSFER_UINT_SAMPLE_BUILTIN) {
  instruction(&s,SpvOpDecorate,0,0,3,SAMPLE,SpvDecorationBuiltIn,SpvBuiltInSampleId);
  instruction(&s,SpvOpDecorate,0,0,2,SAMPLE,SpvDecorationFlat,0);
 }
 instruction(&s,SpvOpDecorate,0,0,3,OUTPUT,SpvDecorationLocation,options->target);
 instruction(&s,SpvOpDecorate,0,0,3,IMAGE,SpvDecorationDescriptorSet,0);
 instruction(&s,SpvOpDecorate,0,0,3,IMAGE,SpvDecorationBinding,0);
 instruction(&s,SpvOpDecorate,0,0,3,TPCARRAY,SpvDecorationArrayStride,4);
 instruction(&s,SpvOpDecorate,0,0,2,TPC,SpvDecorationBlock,0);
 xe_spv_binary_instruction(out,SpvOpMemberDecorate,0,0,member,4);
 instruction(&s,SpvOpTypeVoid,0,TV,0,0,0,0);
 instruction(&s,SpvOpTypeInt,0,TU,2,32,0,0);
 instruction(&s,SpvOpTypeBool,0,TB,0,0,0,0);
 instruction(&s,SpvOpTypeFloat,0,TF,1,32,0,0);
 instruction(&s,SpvOpTypeVector,0,TU2,2,TU,2,0);
 instruction(&s,SpvOpTypeVector,0,TU4,2,TU,4,0);
 instruction(&s,SpvOpTypeVector,0,TF4,2,TF,4,0);
 instruction(&s,SpvOpTypeStruct,0,TPAIR,2,TU,TU,0);
 for(i=0;i<=32;++i)instruction(&s,SpvOpConstant,TU,literal(i),1,i,0,0);
 instruction(&s,SpvOpConstant,TU,literal(33),1,0xfffffffeu,0,0);
 instruction(&s,SpvOpConstant,TU,literal(34),1,0xfffffffdu,0,0);
 instruction(&s,SpvOpConstant,TU,literal(35),1,0xfffffffcu,0,0);
 instruction(&s,SpvOpConstant,TU,literal(36),1,0xffffffffu,0,0);
 instruction(&s,SpvOpConstant,TU,literal(37),1,2047,0,0);
 image_type[4]=options->source_multisampled;
 xe_spv_binary_instruction(out,SpvOpTypeImage,0,TIMAGE,image_type,7);
 instruction(&s,SpvOpTypeArray,0,TPCARRAY,2,TU,literal(28),0);
 instruction(&s,SpvOpTypeStruct,0,TPC,1,TPCARRAY,0,0);
 instruction(&s,SpvOpTypePointer,0,PIMAGE,2,SpvStorageClassUniformConstant,TIMAGE,0);
 instruction(&s,SpvOpTypePointer,0,PPC,2,SpvStorageClassPushConstant,TPC,0);
 instruction(&s,SpvOpTypePointer,0,PPU,2,SpvStorageClassPushConstant,TU,0);
 instruction(&s,SpvOpTypePointer,0,POUT,2,SpvStorageClassOutput,TU4,0);
 instruction(&s,SpvOpTypePointer,0,PPOS,2,SpvStorageClassInput,TF4,0);
 if(options->sample_mode==XE_TRANSFER_UINT_SAMPLE_BUILTIN)
  instruction(&s,SpvOpTypePointer,0,PSAMPLE,2,SpvStorageClassInput,TU,0);
 instruction(&s,SpvOpTypeFunction,0,TFUNC,1,TV,0,0);
 instruction(&s,SpvOpVariable,PIMAGE,IMAGE,1,SpvStorageClassUniformConstant,0,0);
 instruction(&s,SpvOpVariable,PPC,CONSTANTS,1,SpvStorageClassPushConstant,0,0);
 instruction(&s,SpvOpVariable,POUT,OUTPUT,1,SpvStorageClassOutput,0,0);
 instruction(&s,SpvOpVariable,PPOS,POSITION,1,SpvStorageClassInput,0,0);
 if(options->sample_mode==XE_TRANSFER_UINT_SAMPLE_BUILTIN)
  instruction(&s,SpvOpVariable,PSAMPLE,SAMPLE,1,SpvStorageClassInput,0,0);
 instruction(&s,SpvOpFunction,TV,MAIN,2,0,TFUNC,0);
 instruction(&s,SpvOpLabel,0,LABEL,0,0,0,0);
 for(i=0;i<XE_ADDR_WORDS;++i)address.word[i]=parameter(&s,i);
 position=value(&s,SpvOpLoad,TF4,1,POSITION,0,0);
 x=value(&s,SpvOpCompositeExtract,TF,2,position,0,0);
 y=value(&s,SpvOpCompositeExtract,TF,2,position,1,0);
 x=value(&s,SpvOpConvertFToU,TU,1,x,0,0);y=value(&s,SpvOpConvertFToU,TU,1,y,0,0);
 sample=options->sample_mode==XE_TRANSFER_UINT_SAMPLE_BUILTIN?
  value(&s,SpvOpLoad,TU,1,SAMPLE,0,0):options->sample_mode==XE_TRANSFER_UINT_SAMPLE_CONSTANT?
  parameter(&s,27):literal(0);
 memset(&adapter,0,sizeof(adapter));adapter.out=out;adapter.next=&s.next;
 adapter.uint_type=TU;adapter.bool_type=TB;adapter.pair_type=TPAIR;
 adapter.zero=literal(0);adapter.one=literal(1);adapter.two=literal(2);
 adapter.not_one=literal(33);adapter.not_two=literal(34);adapter.not_three=literal(35);
 adapter.all=literal(36);adapter.tile_mask=literal(37);
 e.emit=xe_transfer_sample_spv_emit;e.context=&adapter;
 if(!xe_transfer_address_emit(&e,&address,x,y,sample,&route))goto fail;
 source.halves=parameter(&s,23);source.wide=parameter(&s,24);source.shift16=literal(16);
 dest.halves=parameter(&s,25);dest.wide=parameter(&s,26);dest.shift16=literal(16);
 image=value(&s,SpvOpLoad,TIMAGE,1,IMAGE,0,0);
 if(!fetch(&s,image,(uint32_t)route.x,(uint32_t)route.y,(uint32_t)route.sample,
   options->source_multisampled,lanes) || !xe_transfer_uint_color_pack(&e,&source,lanes,first))goto fail;
 condition=value(&s,SpvOpINotEqual,TB,2,(uint32_t)route.two_loads,literal(0),0);
 instruction(&s,SpvOpSelectionMerge,0,0,2,MERGE,0,0);
 instruction(&s,SpvOpBranchConditional,0,0,3,condition,FETCH2,MERGE);
 instruction(&s,SpvOpLabel,0,FETCH2,0,0,0,0);
 if(!fetch(&s,image,(uint32_t)route.second_x,(uint32_t)route.y,(uint32_t)route.sample,
   options->source_multisampled,lanes) || !xe_transfer_uint_color_pack(&e,&source,lanes,second))goto fail;
 instruction(&s,SpvOpBranch,0,0,1,MERGE,0,0);
 instruction(&s,SpvOpLabel,0,MERGE,0,0,0,0);
 second_word=s.next++;args[0]=(uint32_t)first[1];args[1]=LABEL;args[2]=(uint32_t)second[0];args[3]=FETCH2;
 xe_spv_binary_instruction(out,SpvOpPhi,TU,second_word,args,4);
 condition=value(&s,SpvOpINotEqual,TB,2,(uint32_t)route.half,literal(0),0);
 packed[0]=value(&s,SpvOpSelect,TU,3,condition,(uint32_t)first[1],(uint32_t)first[0]);
 packed[1]=second_word;
 if(!xe_transfer_uint_color_unpack(&e,&dest,packed,result))goto fail;
 for(i=0;i<4;++i)args[i]=(uint32_t)result[i];
 position=s.next++;xe_spv_binary_instruction(out,SpvOpCompositeConstruct,TU4,position,args,4);
 instruction(&s,SpvOpStore,0,0,2,OUTPUT,position,0);
 instruction(&s,SpvOpReturn,0,0,0,0,0,0);instruction(&s,SpvOpFunctionEnd,0,0,0,0,0,0);
 if(out->failed)goto fail;
 out->words[3]=s.next;return 1;
fail:out->count=0;return 0;
}
