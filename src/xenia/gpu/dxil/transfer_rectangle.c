/* Four borrowed coordinates, no vertex expansion or coordinate arithmetic. */
#include "transfer_rectangle.h"
#include "libretro/core_transfer_geometry.h"
#include <dxil_module.h>
#include <dxil_function.h>
typedef const struct dxil_value *Value;
static Value ic(struct dxil_module *m,uint32_t n){return dxil_module_get_int32_const(m,(int32_t)n);}
static Value input(struct dxil_module *m,unsigned id,unsigned column,enum overload_type type) {
 Value args[5];const struct dxil_func *fn=dxil_get_function(m,"dx.op.loadInput",type);
 args[0]=ic(m,4);args[1]=ic(m,id);args[2]=ic(m,0);args[3]=dxil_module_get_int8_const(m,(int8_t)column);
 args[4]=dxil_module_get_undef(m,dxil_module_get_int_type(m,32));
 return fn?dxil_emit_call(m,fn,args,5):NULL;
}
int xe_dxil_build_transfer_rectangle(XeDxilShaderBinary *binary) {
 XeDxilShaderModule *shader;struct dxil_module *m;
 Value box[4],index,x,y,cx,cy,args[5];const struct dxil_func *store;
 int in,vertex,out,result=0;unsigned i;
 if(!binary)return 0;
 shader=xe_dxil_shader_create(DXIL_VERTEX_SHADER);if(!shader)return 0;
 m=xe_dxil_shader_module(shader);
 in=xe_dxil_shader_signature(shader,0,"POSITION",0,DXIL_SEM_ARBITRARY,0,0,0,4,0);
 vertex=xe_dxil_shader_signature_typed(shader,0,"SV_VertexID",0,DXIL_SEM_VERTEX_ID,
  DXIL_PROG_SEM_VERTEX_ID,1,0,1,0,DXIL_COMP_TYPE_U32);
 out=xe_dxil_shader_signature(shader,1,"SV_Position",0,DXIL_SEM_POSITION,DXIL_PROG_SEM_POSITION,0,0,4,0);
 if(in<0 || vertex<0 || out<0)goto done;
 for(i=0;i<4;++i){box[i]=input(m,(unsigned)in,i,DXIL_F32);if(!box[i])goto done;}
 index=input(m,(unsigned)vertex,0,DXIL_I32);if(!index)goto done;
 x=dxil_emit_binop(m,DXIL_BINOP_LSHR,ic(m,XE_TRANSFER_D3D_X_MASK),index,0);
 y=dxil_emit_binop(m,DXIL_BINOP_LSHR,ic(m,XE_TRANSFER_D3D_Y_MASK),index,0);
 if(!x || !y)goto done;
 x=dxil_emit_binop(m,DXIL_BINOP_AND,x,ic(m,1),0);y=dxil_emit_binop(m,DXIL_BINOP_AND,y,ic(m,1),0);
 if(!x || !y)goto done;
 cx=dxil_emit_cmp(m,DXIL_ICMP_NE,x,ic(m,0));cy=dxil_emit_cmp(m,DXIL_ICMP_NE,y,ic(m,0));
 if(!cx || !cy)goto done;
 box[0]=dxil_emit_select(m,cx,box[2],box[0]);box[1]=dxil_emit_select(m,cy,box[3],box[1]);
 box[2]=dxil_module_get_float_const(m,0.0f);box[3]=dxil_module_get_float_const(m,1.0f);
 store=dxil_get_function(m,"dx.op.storeOutput",DXIL_F32);if(!store)goto done;
 for(i=0;i<4;++i){
  if(!box[i])goto done;
  args[0]=ic(m,5);args[1]=ic(m,(unsigned)out);args[2]=ic(m,0);
  args[3]=dxil_module_get_int8_const(m,(int8_t)i);args[4]=box[i];
  if(!dxil_emit_call_void(m,store,args,5))goto done;
 }
 if(!dxil_emit_ret_void(m))goto done;
 result=xe_dxil_shader_finish(shader,binary);
done:xe_dxil_shader_destroy(shader);return result;
}
