/* Direct native DXIL, no HLSL/source compiler or color conversion. BSD; C89. */
#include "transfer_identity.h"
#include "transfer_sample.h"
#include "guest_resources.h"
#include <dxil_module.h>
#include <dxil_function.h>
#include "libretro/core_draw_stats.h"

static XeDxilValue integer(struct dxil_module *module,uint32_t n)
{
 return dxil_module_get_int32_const(module,(int32_t)n);
}
static XeDxilValue parameter(XeDxilAlu *alu,XeDxilValue constants,
 const xe_transfer_identity_shape *shape,unsigned word)
{
 if(shape->mask & (UINT32_C(1)<<word))return integer(alu->module,shape->address.word[word]);
 return xe_dxil_cbuffer_word(alu,constants,integer(alu->module,xe_transfer_identity_parameter_word(word)));
}
int xe_dxil_transfer_identity_shader(const xe_transfer_identity_options *options,
 const xe_transfer_address_key *key,XeDxilShaderBinary *out)
{
 xe_transfer_identity_shape shape;
 XeDxilShaderModule *shader;struct dxil_module *module;
 XeDxilAlu alu;XeDxilTransferSample adapter;xe_transfer_sample_emitter emitter;
 xe_transfer_address_parameters address;xe_transfer_address_result route;
 XeDxilValue constants,image,position[2],sample,args[9],color,lane;
 const struct dxil_func *function;
 unsigned i,kind;int target,success=0;
 if(!out || !xe_transfer_identity_shape_prepare(&shape,options,key))return 0;
 XE_DRAW_EVENT(XE_DRAW_UTILITY_PIPELINE,XE_DRAW_TRANSLATE);
 shader=xe_dxil_shader_create(DXIL_PIXEL_SHADER);if(!shader)return 0;
 module=xe_dxil_shader_module(shader);xe_dxil_alu_init(&alu,module);
 if(xe_dxil_shader_signature(shader,0,"SV_Position",0,DXIL_SEM_POSITION,
  DXIL_PROG_SEM_POSITION,0,0,4,DXIL_INTERP_LINEAR_NOPERSPECTIVE)!=0)goto done;
 if(options->sample_mode==XE_TRANSFER_IDENTITY_SAMPLE_BUILTIN &&
  !xe_dxil_shader_sample_frequency(shader))goto done;
 target=xe_dxil_shader_signature_typed(shader,1,"SV_Target",options->target,
  DXIL_SEM_TARGET,DXIL_PROG_SEM_TARGET,options->target,0,4,0,DXIL_COMP_TYPE_F32);
 if(target<0)goto done;
 kind=options->source_multisampled?DXIL_RESOURCE_KIND_TEXTURE2DMS:DXIL_RESOURCE_KIND_TEXTURE2D;
 if(xe_dxil_shader_resource_typed(shader,XE_DXIL_RESOURCE_SRV,kind,0,0,1,0,DXIL_COMP_TYPE_F32)!=0 ||
  xe_dxil_shader_resource(shader,XE_DXIL_RESOURCE_CBV,DXIL_RESOURCE_KIND_CBUFFER,
   31,1,1,(unsigned)sizeof(xe_transfer_identity_packet))!=0)goto done;
 image=xe_dxil_resource_handle(&alu,XE_DXIL_RESOURCE_SRV,0,0);
 constants=xe_dxil_resource_handle(&alu,XE_DXIL_RESOURCE_CBV,0,1);
 if(!image || !constants || !xe_dxil_transfer_sample_init(&adapter,&alu))goto done;
 emitter.emit=xe_dxil_transfer_sample_emit;emitter.context=&adapter;
 for(i=0;i<XE_ADDR_WORDS;++i) {
  address.word[i]=(uintptr_t)parameter(&alu,constants,&shape,i);
  if(!address.word[i])goto done;
 }
 function=dxil_get_function(module,"dx.op.loadInput",DXIL_F32);
 args[0]=integer(module,4);args[1]=integer(module,0);args[2]=args[1];
 args[4]=dxil_module_get_undef(module,dxil_module_get_int_type(module,32));
 for(i=0;i<2;++i) {
  args[3]=dxil_module_get_int8_const(module,(int8_t)i);
  position[i]=function?dxil_emit_call(module,function,args,5):NULL;
  position[i]=position[i]?dxil_emit_cast(module,DXIL_CAST_FPTOUI,
   dxil_module_get_int_type(module,32),position[i]):NULL;
  if(!position[i])goto done;
 }
 sample=integer(module,0);
 if(options->sample_mode==XE_TRANSFER_IDENTITY_SAMPLE_BUILTIN) {
  function=dxil_get_function(module,"dx.op.sampleIndex",DXIL_I32);args[0]=integer(module,90);
  sample=function?dxil_emit_call(module,function,args,1):NULL;
 } else if(options->sample_mode==XE_TRANSFER_IDENTITY_SAMPLE_CONSTANT)
  sample=parameter(&alu,constants,&shape,23);
 if(!sample || !xe_transfer_address_emit(&emitter,&address,(uintptr_t)position[0],
  (uintptr_t)position[1],(uintptr_t)sample,&route))goto done;
 function=dxil_get_function(module,"dx.op.textureLoad",DXIL_F32);
 args[0]=integer(module,66);args[1]=image;
 args[2]=options->source_multisampled?(XeDxilValue)route.sample:integer(module,0);
 args[3]=(XeDxilValue)route.x;args[4]=(XeDxilValue)route.y;
 args[5]=dxil_module_get_undef(module,dxil_module_get_int_type(module,32));
 args[6]=integer(module,0);args[7]=args[6];args[8]=args[5];
 for(i=0;i<9;++i)if(!args[i])goto done;
 color=function?dxil_emit_call(module,function,args,9):NULL;if(!color)goto done;
 function=dxil_get_function(module,"dx.op.storeOutput",DXIL_F32);
 args[0]=integer(module,5);args[1]=integer(module,(uint32_t)target);args[2]=integer(module,0);
 for(i=0;i<4;++i) {
  lane=dxil_emit_extractval(module,color,i);
  args[3]=dxil_module_get_int8_const(module,(int8_t)i);args[4]=lane;
  if(!function || !lane || !dxil_emit_call_void(module,function,args,5))goto done;
 }
 if(!alu.failed && dxil_emit_ret_void(module) && xe_dxil_shader_finish(shader,out))success=1;
done:xe_dxil_shader_destroy(shader);return success;
}
