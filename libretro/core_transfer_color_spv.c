/* Initialization-only raw color image shaders. BSD; MSVC C89.
 * Plan bounded declaration metadata, reserve final words once, emit in place.
 * No source compiler, temporary instruction graph or pixel staging storage. */
#include "core_transfer_color_spv.h"
#include "core_transfer_format_spv.h"
#include "core_draw_stats.h"
#include <string.h>
enum { CV=1,CU,CF,CB,CU2,CU3,CU4,CF4,CPAIR,CPCA, CPSC,CPP,CPPU,
 CIMAGE,CPI,CPIF,CPIS,CPO,CPACKT,CUNPACKT,CDIRECTT,CMAINT,CEXT,
 CIMAGEVAR,CPCVAR,CPOSVAR,CSAMPLEVAR,COUTVAR,CMAIN,CPACK,CUNPACK,CDIRECT,
 CCONST=64,CDYNAMIC=320 };
typedef struct color_shader {
 xe_spv_binary *out;
 const xe_transfer_color_options *options;
 const xe_transfer_address_packet_config *config;
 const xe_transfer_address_key *shape;
 xe_transfer_address_config fixed_address;
 uint32_t source_codec,dest_codec;
 uint32_t next;
 size_t words;
 struct {unsigned floating;uint32_t bits;} constants[256];
 uint32_t precise[1024];
 unsigned constant_count,precise_count,planning,failed;
} color_shader;
static uint32_t color_constant(void *opaque,unsigned floating,uint32_t bits) {
 color_shader *s=(color_shader*)opaque;unsigned i;
 for(i=0;i<s->constant_count;++i)
  if(s->constants[i].floating==floating && s->constants[i].bits==bits)return CCONST+i;
 if(!s->planning || i>=256){s->failed=1;return 0;}
 s->constants[i].floating=floating;s->constants[i].bits=bits;++s->constant_count;return CCONST+i;
}
static int color_precise(void *opaque,uint32_t id) {
 color_shader *s=(color_shader*)opaque;unsigned i;
 for(i=0;i<s->precise_count;++i)if(s->precise[i]==id)return 1;
 if(!s->planning || i>=1024){s->failed=1;return 0;}
 s->precise[s->precise_count++]=id;return 1;
}
static uint32_t color_u(color_shader *s,uint32_t value){return color_constant(s,0,value);}
static uint32_t color_id(color_shader *s) {
 if(!s->next || s->next==UINT32_MAX){s->failed=1;return 0;}
 return s->next++;
}
static void color_putv(color_shader *s,unsigned op,uint32_t type,uint32_t result,
 const uint32_t *args,unsigned count) {
 size_t words=1+(type!=0)+(result!=0)+(size_t)count;
 if(words>65535 || s->words>(size_t)-1-words){s->failed=1;return;}
 s->words+=words;
 if(s->out){xe_spv_binary_instruction(s->out,op,type,result,args,count);if(s->out->failed)s->failed=1;}
}
static void color_put(color_shader *s,unsigned op,uint32_t type,uint32_t result,
 unsigned count,uint32_t a,uint32_t b,uint32_t c) {
 uint32_t args[3];args[0]=a;args[1]=b;args[2]=c;color_putv(s,op,type,result,args,count);
}
static uint32_t color_valuev(color_shader *s,unsigned op,uint32_t type,const uint32_t *args,unsigned count) {
 uint32_t id=color_id(s);color_putv(s,op,type,id,args,count);return s->failed?0:id;
}
static uint32_t color_value(color_shader *s,unsigned op,uint32_t type,unsigned count,uint32_t a,uint32_t b,uint32_t c) {
 uint32_t args[3];args[0]=a;args[1]=b;args[2]=c;return color_valuev(s,op,type,args,count);
}
static uint32_t color_source_type(const color_shader *s){return s->options->source_uint?CU4:CF4;}
static uint32_t color_dest_type(const color_shader *s){return s->options->dest_uint?CU4:CF4;}
static void color_format(color_shader *s,xe_transfer_format_spv *adapter,xe_transfer_format_emitter *e) {
 memset(adapter,0,sizeof(*adapter));adapter->out=s->out;adapter->next=&s->next;adapter->word_count=&s->words;
 adapter->uint_type=CU;adapter->float_type=CF;adapter->bool_type=CB;adapter->extended_set=CEXT;
 adapter->constant=color_constant;adapter->precise=color_precise;adapter->owner=s;
 e->emit=xe_transfer_format_spv_emit;e->context=adapter;e->failed=0;
}
static uintptr_t color_address(void *opaque,enum xe_transfer_sample_op op,uintptr_t a,uintptr_t b,uintptr_t c) {
 color_shader *s=(color_shader*)opaque;uint32_t code,type=CU,id;
 if(op==XE_TRANSFER_SAMPLE_CONST)return a<=UINT32_MAX?color_u(s,(uint32_t)a):0;
 if(!a || !b || a>UINT32_MAX || b>UINT32_MAX || c>UINT32_MAX){s->failed=1;return 0;}
 switch(op){
  case XE_TRANSFER_SAMPLE_MUL_HIGH:code=SpvOpUMulExtended;type=CPAIR;break;
  case XE_TRANSFER_SAMPLE_EQ:code=SpvOpIEqual;type=CB;break;
  case XE_TRANSFER_SAMPLE_MUL:code=SpvOpIMul;break;
  case XE_TRANSFER_SAMPLE_SUB:code=SpvOpISub;break;
  case XE_TRANSFER_SAMPLE_GE:code=SpvOpUGreaterThanEqual;type=CB;break;
  case XE_TRANSFER_SAMPLE_ADD:code=SpvOpIAdd;break;
  case XE_TRANSFER_SAMPLE_SELECT:code=SpvOpSelect;if(!c){s->failed=1;return 0;}break;
  case XE_TRANSFER_SAMPLE_AND:code=SpvOpBitwiseAnd;break;
  case XE_TRANSFER_SAMPLE_OR:code=SpvOpBitwiseOr;break;
  case XE_TRANSFER_SAMPLE_XOR:code=SpvOpBitwiseXor;break;
  case XE_TRANSFER_SAMPLE_SHR:code=SpvOpShiftRightLogical;break;
  case XE_TRANSFER_SAMPLE_SHL:code=SpvOpShiftLeftLogical;break;
  default:s->failed=1;return 0;
 }
 id=color_value(s,code,type,op==XE_TRANSFER_SAMPLE_SELECT?3:2,(uint32_t)a,(uint32_t)b,(uint32_t)c);
 if(op==XE_TRANSFER_SAMPLE_MUL_HIGH)id=color_value(s,SpvOpCompositeExtract,CU,2,id,1,0);
 return id;
}
static uint32_t color_parameter(color_shader *s,unsigned word) {
 uint32_t p=color_value(s,SpvOpAccessChain,CPPU,3,CPCVAR,color_u(s,0),color_u(s,word));
 return color_value(s,SpvOpLoad,CU,1,p,0,0);
}
static uint32_t color_vector(color_shader *s,uint32_t type,const uintptr_t *ids,unsigned count) {
 uint32_t args[4];unsigned i;
 for(i=0;i<count;++i){if(!ids[i] || ids[i]>UINT32_MAX){s->failed=1;return 0;}args[i]=(uint32_t)ids[i];}
 return color_valuev(s,SpvOpCompositeConstruct,type,args,count);
}
#ifdef XE_TRANSFER_COLOR_DYNAMIC_TEST
static void color_return(color_shader *s,uint32_t value) {
 color_put(s,SpvOpReturnValue,0,0,1,value,0,0);
}
static int color_codec_function(color_shader *s,unsigned pack) {
 static const uint32_t floats[4]={0,1,2,3},integers[4]={4,5,14,15};
 const uint32_t *formats=(pack?s->options->source_uint:s->options->dest_uint)?integers:floats;
 uint32_t type=pack?CU2:color_dest_type(s),input_type=pack?color_source_type(s):CU2;
 uint32_t integer=pack?s->options->source_uint:1,fmt,data,label,merge,blocks[4],cases[10],zero_vector;
 uintptr_t input[4],output[4];unsigned i,j,n;
 xe_transfer_format_spv adapter;xe_transfer_format_emitter e;color_format(s,&adapter,&e);
 color_put(s,SpvOpFunction,type,pack?CPACK:CUNPACK,2,0,pack?CPACKT:CUNPACKT,0);
 fmt=color_value(s,SpvOpFunctionParameter,CU,0,0,0,0);
 data=color_value(s,SpvOpFunctionParameter,input_type,0,0,0,0);
 label=color_id(s);color_put(s,SpvOpLabel,0,label,0,0,0,0);
 for(i=0;i<(pack?4u:2u);++i)input[i]=color_value(s,SpvOpCompositeExtract,integer?CU:CF,2,data,i,0);
 for(i=0;i<(pack?2u:4u);++i)output[i]=(pack || s->options->dest_uint)?xe_tf_u(&e,0):xe_tf_f(&e,0);
 zero_vector=color_vector(s,type,output,pack?2:4);merge=color_id(s);
 cases[0]=fmt;cases[1]=merge;
 for(i=0;i<4;++i){blocks[i]=color_id(s);cases[2+i*2]=formats[i];cases[3+i*2]=blocks[i];}
 color_put(s,SpvOpSelectionMerge,0,0,2,merge,0,0);color_putv(s,SpvOpSwitch,0,0,cases,10);
 for(i=0;i<4;++i){
  color_put(s,SpvOpLabel,0,blocks[i],0,0,0,0);
  n=pack?xe_transfer_format_pack_color(&e,formats[i],s->options->source_uint,input,output):
   xe_transfer_format_unpack_color(&e,formats[i],s->options->dest_uint,input,output);
  if(!n)return 0;
  j=color_vector(s,type,output,pack?2:4);color_return(s,j);
 }
 color_put(s,SpvOpLabel,0,merge,0,0,0,0);color_return(s,zero_vector);
 color_put(s,SpvOpFunctionEnd,0,0,0,0,0,0);return !s->failed && !e.failed;
}
static int color_direct_function(color_shader *s) {
 uint32_t source,dest,data,label,same,convert,is_zero,decode,encode,merge,decoded,encoded,result,phi[4];
 uintptr_t input[4],output[4];unsigned i;
 xe_transfer_format_spv adapter;xe_transfer_format_emitter e;color_format(s,&adapter,&e);
 color_put(s,SpvOpFunction,CF4,CDIRECT,2,0,CDIRECTT,0);
 source=color_value(s,SpvOpFunctionParameter,CU,0,0,0,0);dest=color_value(s,SpvOpFunctionParameter,CU,0,0,0,0);
 data=color_value(s,SpvOpFunctionParameter,CF4,0,0,0,0);
 label=color_id(s);color_put(s,SpvOpLabel,0,label,0,0,0,0);
 result=color_value(s,SpvOpIEqual,CB,2,source,dest,0);same=color_id(s);convert=color_id(s);
 color_put(s,SpvOpSelectionMerge,0,0,2,convert,0,0);color_put(s,SpvOpBranchConditional,0,0,3,result,same,convert);
 color_put(s,SpvOpLabel,0,same,0,0,0,0);color_return(s,data);color_put(s,SpvOpLabel,0,convert,0,0,0,0);
 for(i=0;i<4;++i)input[i]=color_value(s,SpvOpCompositeExtract,CF,2,data,i,0);
 is_zero=color_value(s,SpvOpIEqual,CB,2,source,color_u(s,0),0);
 decode=color_id(s);encode=color_id(s);merge=color_id(s);
 color_put(s,SpvOpSelectionMerge,0,0,2,merge,0,0);color_put(s,SpvOpBranchConditional,0,0,3,is_zero,decode,encode);
 color_put(s,SpvOpLabel,0,decode,0,0,0,0);
 if(!xe_transfer_format_direct_color(&e,0,1,0,input,output))return 0;
 decoded=color_vector(s,CF4,output,4);color_put(s,SpvOpBranch,0,0,1,merge,0,0);
 color_put(s,SpvOpLabel,0,encode,0,0,0,0);
 if(!xe_transfer_format_direct_color(&e,1,0,0,input,output))return 0;
 encoded=color_vector(s,CF4,output,4);color_put(s,SpvOpBranch,0,0,1,merge,0,0);
 color_put(s,SpvOpLabel,0,merge,0,0,0,0);phi[0]=decoded;phi[1]=decode;phi[2]=encoded;phi[3]=encode;
 result=color_valuev(s,SpvOpPhi,CF4,phi,4);color_return(s,result);
 color_put(s,SpvOpFunctionEnd,0,0,0,0,0,0);return !s->failed && !e.failed;
}
#endif
static uint32_t color_fetch(color_shader *s,uint32_t image,uint32_t x,uint32_t y,uint32_t sample) {
 uint32_t args[4];args[0]=image;args[1]=color_value(s,SpvOpCompositeConstruct,CU2,2,x,y,0);
 args[2]=s->options->source_multisampled?SpvImageOperandsSampleMask:SpvImageOperandsLodMask;
 args[3]=s->options->source_multisampled?sample:color_u(s,0);
 return color_valuev(s,SpvOpImageFetch,color_source_type(s),args,4);
}
#ifdef XE_TRANSFER_COLOR_DYNAMIC_TEST
static int color_main(color_shader *s) {
 xe_transfer_sample_emitter e;xe_transfer_address_parameters address;xe_transfer_address_result route;
 uintptr_t packet[6];uint32_t label,current,position,x,y,sample,source,dest,image,first,packed,low,high;
 uint32_t condition,a,b,direct=0,generic=0,finish=0,fetch2,merge,second,second_word,pair,result,args[4];unsigned i;
 color_put(s,SpvOpFunction,CV,CMAIN,2,0,CMAINT,0);label=color_id(s);current=label;
 color_put(s,SpvOpLabel,0,label,0,0,0,0);
 position=color_value(s,SpvOpLoad,CF4,1,CPOSVAR,0,0);
 x=color_value(s,SpvOpCompositeExtract,CF,2,position,0,0);y=color_value(s,SpvOpCompositeExtract,CF,2,position,1,0);
 x=color_value(s,SpvOpConvertFToU,CU,1,x,0,0);y=color_value(s,SpvOpConvertFToU,CU,1,y,0,0);
 for(i=0;i<6;++i)packet[i]=color_parameter(s,i);
 source=color_parameter(s,6);dest=color_parameter(s,7);
 sample=s->options->sample_mode==XE_TRANSFER_COLOR_SAMPLE_BUILTIN?color_value(s,SpvOpLoad,CU,1,CSAMPLEVAR,0,0):
  s->options->sample_mode==XE_TRANSFER_COLOR_SINGLE?color_u(s,0):(uint32_t)packet[XE_AP_SAMPLE];
 e.emit=color_address;e.context=s;
 if(!xe_transfer_address_packet_emit(&e,s->config,packet,&address) || !xe_transfer_address_emit(&e,&address,x,y,sample,&route))return 0;
 image=color_value(s,SpvOpLoad,CIMAGE,1,CIMAGEVAR,0,0);
 first=color_fetch(s,image,(uint32_t)route.x,(uint32_t)route.y,(uint32_t)route.sample);
 if(!s->options->source_uint && !s->options->dest_uint){
  condition=color_value(s,SpvOpIEqual,CB,2,source,dest,0);
  a=color_value(s,SpvOpULessThan,CB,2,source,color_u(s,2),0);b=color_value(s,SpvOpULessThan,CB,2,dest,color_u(s,2),0);
  a=color_value(s,SpvOpLogicalAnd,CB,2,a,b,0);condition=color_value(s,SpvOpLogicalOr,CB,2,condition,a,0);
  direct=color_id(s);generic=color_id(s);finish=color_id(s);
  color_put(s,SpvOpSelectionMerge,0,0,2,finish,0,0);color_put(s,SpvOpBranchConditional,0,0,3,condition,direct,generic);
  color_put(s,SpvOpLabel,0,direct,0,0,0,0);args[0]=CDIRECT;args[1]=source;args[2]=dest;args[3]=first;
  result=color_valuev(s,SpvOpFunctionCall,CF4,args,4);color_put(s,SpvOpStore,0,0,2,COUTVAR,result,0);
  color_put(s,SpvOpBranch,0,0,1,finish,0,0);color_put(s,SpvOpLabel,0,generic,0,0,0,0);current=generic;
 }
 packed=color_value(s,SpvOpFunctionCall,CU2,3,CPACK,source,first);
 low=color_value(s,SpvOpCompositeExtract,CU,2,packed,0,0);high=color_value(s,SpvOpCompositeExtract,CU,2,packed,1,0);
 condition=color_value(s,SpvOpINotEqual,CB,2,(uint32_t)route.two_loads,color_u(s,0),0);
 fetch2=color_id(s);merge=color_id(s);color_put(s,SpvOpSelectionMerge,0,0,2,merge,0,0);
 color_put(s,SpvOpBranchConditional,0,0,3,condition,fetch2,merge);color_put(s,SpvOpLabel,0,fetch2,0,0,0,0);
 second=color_fetch(s,image,(uint32_t)route.second_x,(uint32_t)route.y,(uint32_t)route.sample);
 packed=color_value(s,SpvOpFunctionCall,CU2,3,CPACK,source,second);second=color_value(s,SpvOpCompositeExtract,CU,2,packed,0,0);
 color_put(s,SpvOpBranch,0,0,1,merge,0,0);color_put(s,SpvOpLabel,0,merge,0,0,0,0);
 args[0]=high;args[1]=current;args[2]=second;args[3]=fetch2;second_word=color_valuev(s,SpvOpPhi,CU,args,4);
 condition=color_value(s,SpvOpINotEqual,CB,2,(uint32_t)route.half,color_u(s,0),0);
 low=color_value(s,SpvOpSelect,CU,3,condition,high,low);pair=color_value(s,SpvOpCompositeConstruct,CU2,2,low,second_word,0);
 result=color_value(s,SpvOpFunctionCall,color_dest_type(s),3,CUNPACK,dest,pair);color_put(s,SpvOpStore,0,0,2,COUTVAR,result,0);
 if(finish){color_put(s,SpvOpBranch,0,0,1,finish,0,0);color_put(s,SpvOpLabel,0,finish,0,0,0,0);}
 color_put(s,SpvOpReturn,0,0,0,0,0,0);color_put(s,SpvOpFunctionEnd,0,0,0,0,0,0);return !s->failed;
}
#endif
static int color_fixed_main(color_shader *s) {
 xe_transfer_sample_emitter ae;xe_transfer_address_parameters address;xe_transfer_address_result route;
 xe_transfer_format_spv adapter;xe_transfer_format_emitter e;
 uintptr_t lanes[4],raw[2],second_raw[2],out[4];
 uint32_t position,x,y,image,fetched,label,packed,field,low,result;unsigned i;
 color_put(s,SpvOpFunction,CV,CMAIN,2,0,CMAINT,0);label=color_id(s);
 color_put(s,SpvOpLabel,0,label,0,0,0,0);
 position=color_value(s,SpvOpLoad,CF4,1,CPOSVAR,0,0);
 x=color_value(s,SpvOpCompositeExtract,CF,2,position,0,0);y=color_value(s,SpvOpCompositeExtract,CF,2,position,1,0);
 x=color_value(s,SpvOpConvertFToU,CU,1,x,0,0);y=color_value(s,SpvOpConvertFToU,CU,1,y,0,0);
 for(i=0;i<XE_ADDR_WORDS;++i)address.word[i]=color_u(s,s->fixed_address.word[i]);
 packed=color_parameter(s,XE_AP_ADDRESS);
 address.word[XE_ADDR_D_PITCH]=color_value(s,SpvOpBitwiseAnd,CU,2,packed,color_u(s,1023),0);
 field=color_value(s,SpvOpShiftRightLogical,CU,2,packed,color_u(s,10),0);
 address.word[XE_ADDR_S_PITCH]=color_value(s,SpvOpBitwiseAnd,CU,2,field,color_u(s,1023),0);
 field=color_value(s,SpvOpShiftRightLogical,CU,2,packed,color_u(s,20),0);
 field=color_value(s,SpvOpBitwiseXor,CU,2,field,color_u(s,2048),0);
 address.word[XE_ADDR_DELTA]=color_value(s,SpvOpISub,CU,2,field,color_u(s,2048),0);
 address.word[XE_ADDR_S_PR]=color_parameter(s,XE_AP_SOURCE_PR);
 ae.emit=color_address;ae.context=s;
 if(!xe_transfer_address_emit(&ae,&address,x,y,color_u(s,0),&route))return 0;
 image=color_value(s,SpvOpLoad,CIMAGE,1,CIMAGEVAR,0,0);
 fetched=color_fetch(s,image,(uint32_t)route.x,(uint32_t)route.y,(uint32_t)route.sample);
 for(i=0;i<4;++i)lanes[i]=color_value(s,SpvOpCompositeExtract,s->options->source_uint?CU:CF,2,fetched,i,0);
 color_format(s,&adapter,&e);
 if(!s->options->source_uint && !s->options->dest_uint &&
    (s->source_codec==s->dest_codec || (s->source_codec<2 && s->dest_codec<2))){
  if(!xe_transfer_format_direct_color(&e,s->source_codec,s->dest_codec,0,lanes,out))return 0;
 }else{
  if(!xe_transfer_format_pack_color(&e,s->source_codec,s->options->source_uint,lanes,raw))return 0;
  if(s->shape->dest_wide && !s->shape->source_wide){
   fetched=color_fetch(s,image,(uint32_t)route.second_x,(uint32_t)route.y,(uint32_t)route.sample);
   for(i=0;i<4;++i)lanes[i]=color_value(s,SpvOpCompositeExtract,s->options->source_uint?CU:CF,2,fetched,i,0);
   if(!xe_transfer_format_pack_color(&e,s->source_codec,s->options->source_uint,lanes,second_raw))return 0;
   raw[1]=second_raw[0];
  }else if(s->shape->source_wide && !s->shape->dest_wide){
   field=color_value(s,SpvOpINotEqual,CB,2,(uint32_t)route.half,color_u(s,0),0);
   low=color_value(s,SpvOpSelect,CU,3,field,(uint32_t)raw[1],(uint32_t)raw[0]);raw[0]=low;
  }
  if(!xe_transfer_format_unpack_color(&e,s->dest_codec,s->options->dest_uint,raw,out))return 0;
 }
 result=color_vector(s,color_dest_type(s),out,4);color_put(s,SpvOpStore,0,0,2,COUTVAR,result,0);
 color_put(s,SpvOpReturn,0,0,0,0,0,0);color_put(s,SpvOpFunctionEnd,0,0,0,0,0,0);
 return !s->failed && !e.failed;
}
static int color_body(color_shader *s) {
#ifdef XE_TRANSFER_COLOR_DYNAMIC_TEST
 if(!s->shape)return color_codec_function(s,1) && color_codec_function(s,0) &&
  ((s->options->source_uint || s->options->dest_uint) || color_direct_function(s)) && color_main(s);
#endif
 return color_fixed_main(s);
}
static void color_header(color_shader *s,uint32_t bound) {
 uint32_t header[5]={0x07230203,0x00010000,0,0,0},entry[7]={SpvExecutionModelFragment,CMAIN,0x6e69616d,0,CPOSVAR,COUTVAR,CSAMPLEVAR};
 uint32_t image[7],args[4],member[4]={CPSC,0,SpvDecorationOffset,0};unsigned i;
 header[3]=bound;s->words+=5;if(s->out)xe_spv_binary_append(s->out,header,5);
 color_put(s,SpvOpCapability,0,0,1,SpvCapabilityShader,0,0);
 if(s->options->sample_mode==XE_TRANSFER_COLOR_SAMPLE_BUILTIN)color_put(s,SpvOpCapability,0,0,1,SpvCapabilitySampleRateShading,0,0);
 s->words+=6;if(s->out){uint32_t set=CEXT;xe_spv_binary_string_span(s->out,SpvOpExtInstImport,&set,1,"GLSL.std.450",12);}
 color_put(s,SpvOpMemoryModel,0,0,2,SpvAddressingModelLogical,SpvMemoryModelGLSL450,0);
 color_putv(s,SpvOpEntryPoint,0,0,entry,s->options->sample_mode==XE_TRANSFER_COLOR_SAMPLE_BUILTIN?7:6);
 color_put(s,SpvOpExecutionMode,0,0,2,CMAIN,SpvExecutionModeOriginUpperLeft,0);
 color_put(s,SpvOpDecorate,0,0,3,CPOSVAR,SpvDecorationBuiltIn,SpvBuiltInFragCoord);
 if(s->options->sample_mode==XE_TRANSFER_COLOR_SAMPLE_BUILTIN){
  color_put(s,SpvOpDecorate,0,0,3,CSAMPLEVAR,SpvDecorationBuiltIn,SpvBuiltInSampleId);
  color_put(s,SpvOpDecorate,0,0,2,CSAMPLEVAR,SpvDecorationFlat,0);
 }
 color_put(s,SpvOpDecorate,0,0,3,COUTVAR,SpvDecorationLocation,s->options->target);
 color_put(s,SpvOpDecorate,0,0,3,CIMAGEVAR,SpvDecorationDescriptorSet,0);color_put(s,SpvOpDecorate,0,0,3,CIMAGEVAR,SpvDecorationBinding,0);
 color_put(s,SpvOpDecorate,0,0,3,CPCA,SpvDecorationArrayStride,4);color_put(s,SpvOpDecorate,0,0,2,CPSC,SpvDecorationBlock,0);
 color_putv(s,SpvOpMemberDecorate,0,0,member,4);
 for(i=0;i<s->precise_count;++i)color_put(s,SpvOpDecorate,0,0,2,s->precise[i],SpvDecorationNoContraction,0);
 color_put(s,SpvOpTypeVoid,0,CV,0,0,0,0);color_put(s,SpvOpTypeInt,0,CU,2,32,0,0);color_put(s,SpvOpTypeFloat,0,CF,1,32,0,0);
 color_put(s,SpvOpTypeBool,0,CB,0,0,0,0);color_put(s,SpvOpTypeVector,0,CU2,2,CU,2,0);color_put(s,SpvOpTypeVector,0,CU3,2,CU,3,0);
 color_put(s,SpvOpTypeVector,0,CU4,2,CU,4,0);color_put(s,SpvOpTypeVector,0,CF4,2,CF,4,0);color_put(s,SpvOpTypeStruct,0,CPAIR,2,CU,CU,0);
 /* Array length must be declared before the array type, and only once. */
 for(i=0;i<s->constant_count;++i)color_put(s,SpvOpConstant,s->constants[i].floating?CF:CU,CCONST+i,1,s->constants[i].bits,0,0);
 color_put(s,SpvOpTypeArray,0,CPCA,2,CU,color_u(s,8),0);color_put(s,SpvOpTypeStruct,0,CPSC,1,CPCA,0,0);
 color_put(s,SpvOpTypePointer,0,CPP,2,SpvStorageClassPushConstant,CPSC,0);color_put(s,SpvOpTypePointer,0,CPPU,2,SpvStorageClassPushConstant,CU,0);
 image[0]=s->options->source_uint?CU:CF;image[1]=SpvDim2D;image[2]=image[3]=0;image[4]=s->options->source_multisampled;image[5]=1;image[6]=SpvImageFormatUnknown;
 color_putv(s,SpvOpTypeImage,0,CIMAGE,image,7);color_put(s,SpvOpTypePointer,0,CPI,2,SpvStorageClassUniformConstant,CIMAGE,0);
 color_put(s,SpvOpTypePointer,0,CPIF,2,SpvStorageClassInput,CF4,0);color_put(s,SpvOpTypePointer,0,CPIS,2,SpvStorageClassInput,CU,0);
 color_put(s,SpvOpTypePointer,0,CPO,2,SpvStorageClassOutput,color_dest_type(s),0);
 color_put(s,SpvOpTypeFunction,0,CPACKT,3,CU2,CU,color_source_type(s));color_put(s,SpvOpTypeFunction,0,CUNPACKT,3,color_dest_type(s),CU,CU2);
 if(!s->options->source_uint && !s->options->dest_uint){args[0]=CF4;args[1]=CU;args[2]=CU;args[3]=CF4;color_putv(s,SpvOpTypeFunction,0,CDIRECTT,args,4);}
 color_put(s,SpvOpTypeFunction,0,CMAINT,1,CV,0,0);color_put(s,SpvOpVariable,CPI,CIMAGEVAR,1,SpvStorageClassUniformConstant,0,0);
 color_put(s,SpvOpVariable,CPP,CPCVAR,1,SpvStorageClassPushConstant,0,0);color_put(s,SpvOpVariable,CPIF,CPOSVAR,1,SpvStorageClassInput,0,0);
 if(s->options->sample_mode==XE_TRANSFER_COLOR_SAMPLE_BUILTIN)color_put(s,SpvOpVariable,CPIS,CSAMPLEVAR,1,SpvStorageClassInput,0,0);
 color_put(s,SpvOpVariable,CPO,COUTVAR,1,SpvStorageClassOutput,0,0);
}
static int color_build(xe_spv_binary *out,size_t *planned,const xe_transfer_color_options *options,
 const xe_transfer_address_packet_config *config,const xe_transfer_address_key *shape,unsigned source_format,unsigned dest_format) {
 color_shader s;size_t body_words,total;uint32_t bound;
 xe_transfer_color_packet packet;
#ifndef XE_TRANSFER_COLOR_DYNAMIC_TEST
 if(!shape)return 0;
#endif
 if((!out && !planned) || !options || options->source_uint>1 || options->dest_uint>1 || options->source_multisampled>1 ||
    options->sample_mode>XE_TRANSFER_COLOR_SAMPLE_CONSTANT || options->target>3 || !xe_transfer_address_packet_config_valid(config))return 0;
 XE_DRAW_EVENT(XE_DRAW_UTILITY_PIPELINE,XE_DRAW_TRANSLATE);
 memset(&s,0,sizeof(s));s.options=options;s.config=config;s.shape=shape;
 if(shape){
  if(options->source_multisampled || options->sample_mode!=XE_TRANSFER_COLOR_SINGLE ||
     shape->source_msaa || shape->dest_msaa ||
     !xe_transfer_color_packet_prepare(&packet,options,config,shape,source_format,dest_format,0))return 0;
  s.source_codec=packet.source_codec;s.dest_codec=packet.dest_codec;
  if(!xe_transfer_address_prepare(&s.fixed_address,shape))return 0;
 }
 s.planning=1;s.next=CDYNAMIC;color_u(&s,8);
 if(!color_body(&s))goto failed;
 body_words=s.words;bound=s.next;s.planning=0;s.words=0;color_header(&s,bound);
 if(s.failed || body_words>(size_t)-1-s.words)goto failed;
 total=body_words+s.words;
 if(planned){*planned=total;return 1;}
 xe_spv_binary_reset(out);if(!xe_spv_binary_reserve_exact(out,total))goto failed;
 s.out=out;s.words=0;s.next=CDYNAMIC;color_header(&s,bound);
 if(!color_body(&s) || out->failed || s.words!=total || out->count!=total || s.next!=bound)goto failed;
 return 1;
failed:if(out)out->count=0;return 0;
}
#ifdef XE_TRANSFER_COLOR_DYNAMIC_TEST
int xe_transfer_color_spv_build(xe_spv_binary *out,const xe_transfer_color_options *options,
 const xe_transfer_address_packet_config *config){
 return out && color_build(out,NULL,options,config,NULL,0,0);
}
#endif
int xe_transfer_color_spv_shape_words(size_t *words,const xe_transfer_color_options *options,
 const xe_transfer_address_packet_config *config,const xe_transfer_address_key *key,unsigned source,unsigned dest){
 return words && key && color_build(NULL,words,options,config,key,source,dest);
}
int xe_transfer_color_spv_build_shape(xe_spv_binary *out,const xe_transfer_color_options *options,
 const xe_transfer_address_packet_config *config,const xe_transfer_address_key *key,unsigned source,unsigned dest){
 return out && key && color_build(out,NULL,options,config,key,source,dest);
}
