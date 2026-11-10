/* Raw native DXIL, no HLSL/source compiler. BSD; MSVC C89. */
#include "transfer_uint.h"
#include "transfer_sample.h"
#include "guest_resources.h"
#include <dxil_module.h>
#include <dxil_function.h>
static XeDxilValue integer(struct dxil_module *m,uint32_t n) {
 return dxil_module_get_int32_const(m,(int32_t)n);
}
static int begin(struct dxil_module *m,unsigned block) {
 struct dxil_func_def *f=m->cur_emitting_func;
 if(block>=f->num_basic_block_ids || f->basic_block_ids[block]!=-1)return 0;
 f->basic_block_ids[block]=(int)f->curr_block;return 1;
}
static int fetch(XeDxilAlu *alu,XeDxilValue image,unsigned multisampled,
 uintptr_t x,uintptr_t y,uintptr_t sample,uintptr_t lanes[4]) {
 struct dxil_module *m=alu->module;
 const struct dxil_func *function=dxil_get_function(m,"dx.op.textureLoad",DXIL_I32);
 XeDxilValue args[9],value;unsigned i;
 args[0]=integer(m,66);args[1]=image;
 args[2]=multisampled?(XeDxilValue)sample:integer(m,0);
 args[3]=(XeDxilValue)x;args[4]=(XeDxilValue)y;
 args[5]=dxil_module_get_undef(m,dxil_module_get_int_type(m,32));
 args[6]=integer(m,0);args[7]=args[6];args[8]=args[5];
 for(i=0;i<9;++i)if(!args[i])return 0;
 value=function?dxil_emit_call(m,function,args,9):NULL;if(!value)return 0;
 for(i=0;i<4;++i){lanes[i]=(uintptr_t)dxil_emit_extractval(m,value,i);if(!lanes[i])return 0;}
 return 1;
}
int xe_dxil_transfer_uint_shader(const xe_transfer_uint_options *options,
 XeDxilShaderBinary *out) {
 XeDxilShaderModule *shader;struct dxil_module *m;
 XeDxilAlu alu;XeDxilTransferSample adapter;xe_transfer_sample_emitter e;
 xe_transfer_address_parameters address;xe_transfer_address_result route;
 xe_transfer_uint_color_parameters source,dest;
 uintptr_t lanes[4],first[2],second[2],packed[2],result[4];
 XeDxilValue constants,image,position[2],sample,args[5],condition,incoming[2];
 const struct dxil_func *function;struct dxil_instr *phi;
 unsigned i,kind,blocks,parents[2];int target,success=0;
 if(!out || !options || options->source_multisampled>1 ||
    options->sample_mode>XE_TRANSFER_UINT_SAMPLE_CONSTANT || options->target>3)return 0;
 shader=xe_dxil_shader_create(DXIL_PIXEL_SHADER);if(!shader)return 0;
 m=xe_dxil_shader_module(shader);xe_dxil_alu_init(&alu,m);
 if(xe_dxil_shader_signature(shader,0,"SV_Position",0,DXIL_SEM_POSITION,
   DXIL_PROG_SEM_POSITION,0,0,4,DXIL_INTERP_LINEAR_NOPERSPECTIVE)!=0)goto done;
 if(options->sample_mode==XE_TRANSFER_UINT_SAMPLE_BUILTIN &&
   !xe_dxil_shader_sample_frequency(shader))goto done;
 target=xe_dxil_shader_signature_typed(shader,1,"SV_Target",options->target,
  DXIL_SEM_TARGET,DXIL_PROG_SEM_TARGET,options->target,0,4,0,DXIL_COMP_TYPE_U32);
 if(target<0)goto done;
 kind=options->source_multisampled?DXIL_RESOURCE_KIND_TEXTURE2DMS:DXIL_RESOURCE_KIND_TEXTURE2D;
 if(xe_dxil_shader_resource_typed(shader,XE_DXIL_RESOURCE_SRV,kind,0,0,1,0,DXIL_COMP_TYPE_U32)!=0 ||
    xe_dxil_shader_resource(shader,XE_DXIL_RESOURCE_CBV,DXIL_RESOURCE_KIND_CBUFFER,
     31,1,1,(unsigned)sizeof(xe_transfer_uint_packet))!=0)goto done;
 image=xe_dxil_resource_handle(&alu,XE_DXIL_RESOURCE_SRV,0,0);
 constants=xe_dxil_resource_handle(&alu,XE_DXIL_RESOURCE_CBV,0,1);
 if(!image || !constants || !xe_dxil_transfer_sample_init(&adapter,&alu))goto done;
 e.emit=xe_dxil_transfer_sample_emit;e.context=&adapter;
 for(i=0;i<XE_ADDR_WORDS;++i) {
  address.word[i]=(uintptr_t)xe_dxil_cbuffer_word(&alu,constants,integer(m,i));
  if(!address.word[i])goto done;
 }
 function=dxil_get_function(m,"dx.op.loadInput",DXIL_F32);
 args[0]=integer(m,4);args[1]=integer(m,0);args[2]=args[1];
 args[4]=dxil_module_get_undef(m,dxil_module_get_int_type(m,32));
 for(i=0;i<2;++i) {
  args[3]=dxil_module_get_int8_const(m,(int8_t)i);
  position[i]=function?dxil_emit_call(m,function,args,5):NULL;
  position[i]=position[i]?dxil_emit_cast(m,DXIL_CAST_FPTOUI,dxil_module_get_int_type(m,32),position[i]):NULL;
  if(!position[i])goto done;
 }
 sample=integer(m,0);
 if(options->sample_mode==XE_TRANSFER_UINT_SAMPLE_BUILTIN) {
  function=dxil_get_function(m,"dx.op.sampleIndex",DXIL_I32);args[0]=integer(m,90);
  sample=function?dxil_emit_call(m,function,args,1):NULL;
 } else if(options->sample_mode==XE_TRANSFER_UINT_SAMPLE_CONSTANT)
  sample=xe_dxil_cbuffer_word(&alu,constants,integer(m,27));
 if(!sample || !xe_transfer_address_emit(&e,&address,(uintptr_t)position[0],
   (uintptr_t)position[1],(uintptr_t)sample,&route))goto done;
 source.halves=(uintptr_t)xe_dxil_cbuffer_word(&alu,constants,integer(m,23));
 source.wide=(uintptr_t)xe_dxil_cbuffer_word(&alu,constants,integer(m,24));source.shift16=(uintptr_t)integer(m,16);
 dest.halves=(uintptr_t)xe_dxil_cbuffer_word(&alu,constants,integer(m,25));
 dest.wide=(uintptr_t)xe_dxil_cbuffer_word(&alu,constants,integer(m,26));dest.shift16=source.shift16;
 if(!fetch(&alu,image,options->source_multisampled,route.x,route.y,route.sample,lanes) ||
    !xe_transfer_uint_color_pack(&e,&source,lanes,first))goto done;
 condition=dxil_emit_cmp(m,DXIL_ICMP_NE,(XeDxilValue)route.two_loads,integer(m,0));
 if(!condition || !dxil_function_reserve_blocks(m,2,&blocks))goto done;
 parents[0]=0;parents[1]=blocks;incoming[0]=(XeDxilValue)first[1];
 if(!dxil_emit_branch(m,condition,blocks,blocks+1) || !begin(m,blocks))goto done;
 if(!fetch(&alu,image,options->source_multisampled,route.second_x,route.y,route.sample,lanes) ||
    !xe_transfer_uint_color_pack(&e,&source,lanes,second))goto done;
 incoming[1]=(XeDxilValue)second[0];
 if(!dxil_emit_branch(m,NULL,blocks+1,0) || !begin(m,blocks+1))goto done;
 phi=dxil_emit_phi(m,dxil_module_get_int_type(m,32));
 if(!phi || !dxil_phi_add_incoming(phi,incoming,parents,2))goto done;
 packed[1]=(uintptr_t)dxil_instr_get_return_value(phi);
 condition=dxil_emit_cmp(m,DXIL_ICMP_NE,(XeDxilValue)route.half,integer(m,0));
 packed[0]=condition?(uintptr_t)dxil_emit_select(m,condition,(XeDxilValue)first[1],(XeDxilValue)first[0]):0;
 if(!packed[0] || !packed[1] || !xe_transfer_uint_color_unpack(&e,&dest,packed,result))goto done;
 function=dxil_get_function(m,"dx.op.storeOutput",DXIL_I32);
 args[0]=integer(m,5);args[1]=integer(m,(uint32_t)target);args[2]=integer(m,0);
 for(i=0;i<4;++i) {
  args[3]=dxil_module_get_int8_const(m,(int8_t)i);args[4]=(XeDxilValue)result[i];
  if(!function || !dxil_emit_call_void(m,function,args,5))goto done;
 }
 if(!alu.failed && dxil_emit_ret_void(m) && xe_dxil_shader_finish(shader,out))success=1;
done:xe_dxil_shader_destroy(shader);return success;
}
