/* Runtime sample-address module; native DXIL, no source compiler. BSD; C89. */
#include "src/xenia/gpu/dxil/transfer_sample.h"
#include "src/xenia/gpu/dxil/guest_resources.h"
#include "src/xenia/gpu/dxil/shader_module.h"
#ifdef XE_TRANSFER_UINT_COLOR_TEST
#include "libretro/core_transfer_uint_color.h"
#endif
#include <dxil_module.h>
#include <dxil_function.h>
#include <stdio.h>
#include <string.h>
static XeDxilValue integer(struct dxil_module *m,uint32_t v) {
 return dxil_module_get_int32_const(m,(int32_t)v);
}
static XeDxilValue field(XeDxilAlu *s,XeDxilValue handle,XeDxilValue base,unsigned n) {
 XeDxilValue offset=dxil_emit_binop(s->module,DXIL_BINOP_ADD,base,integer(s->module,n*4),0);
 return offset?xe_dxil_raw_buffer_load(s,handle,offset):NULL;
}
static int block(struct dxil_module *m,unsigned id) {
 struct dxil_func_def *f=m->cur_emitting_func;
 if(id>=f->num_basic_block_ids || f->basic_block_ids[id]!=-1)return 0;
 f->basic_block_ids[id]=(int)f->curr_block;return 1;
}
int main(int argc,char **argv) {
 XeDxilShaderModule *shader=NULL;XeDxilShaderBinary binary={0};struct dxil_module *m;
 XeDxilAlu alu;XeDxilTransferSample adapter;xe_transfer_sample_emitter e;
#ifdef XE_TRANSFER_UINT_COLOR_TEST
 xe_transfer_uint_color_parameters p;uintptr_t source[4],packed[2],result[4];
#else
 xe_transfer_sample_parameters p;xe_transfer_sample_coordinates canonical,inverse,result;
#endif
 XeDxilValue input,output,cbv,index,args[2],base,dest,limit,a,b,c,valid,values[7];
 const struct dxil_func *thread;unsigned blocks,i;FILE *file;int failed=1;char path[4096];
 if(argc!=2 || strlen(argv[1])>sizeof(path)-6)return 1;
 shader=xe_dxil_shader_create(DXIL_COMPUTE_SHADER);if(!shader)return 1;
 m=xe_dxil_shader_module(shader);xe_dxil_alu_init(&alu,m);
 if(!xe_dxil_shader_threads(shader,64,1,1) ||
  xe_dxil_shader_resource(shader,XE_DXIL_RESOURCE_UAV,DXIL_RESOURCE_KIND_RAW_BUFFER,0,0,1,0)!=0 ||
  xe_dxil_shader_resource(shader,XE_DXIL_RESOURCE_SRV,DXIL_RESOURCE_KIND_RAW_BUFFER,0,0,1,0)!=0 ||
  xe_dxil_shader_resource(shader,XE_DXIL_RESOURCE_CBV,DXIL_RESOURCE_KIND_CBUFFER,0,0,1,16)!=0)goto done;
 output=xe_dxil_resource_handle(&alu,XE_DXIL_RESOURCE_UAV,0,0);
 input=xe_dxil_resource_handle(&alu,XE_DXIL_RESOURCE_SRV,0,0);
 cbv=xe_dxil_resource_handle(&alu,XE_DXIL_RESOURCE_CBV,0,0);
 thread=dxil_get_function(m,"dx.op.threadId",DXIL_I32);
 args[0]=integer(m,93);args[1]=integer(m,0);index=thread?dxil_emit_call(m,thread,args,2):NULL;
 if(!input || !output || !cbv || !index)goto done;
 limit=xe_dxil_cbuffer_word(&alu,cbv,integer(m,0));if(!limit)goto done;
 a=dxil_emit_cmp(m,DXIL_ICMP_ULT,index,limit);
 limit=xe_dxil_cbuffer_word(&alu,cbv,integer(m,1));if(!limit)goto done;
 b=dxil_emit_cmp(m,DXIL_ICMP_ULT,index,limit);
 valid=a && b?dxil_emit_binop(m,DXIL_BINOP_AND,a,b,0):NULL;
 if(!valid || !dxil_function_reserve_blocks(m,2,&blocks) ||
    !dxil_emit_branch(m,valid,blocks,blocks+1) || !block(m,blocks))goto done;
 base=dxil_emit_binop(m,DXIL_BINOP_MUL,index,integer(m,128),0);
 dest=dxil_emit_binop(m,DXIL_BINOP_MUL,index,integer(m,64),0);
 if(!base || !dest || !xe_dxil_transfer_sample_init(&adapter,&alu))goto done;
 e.emit=xe_dxil_transfer_sample_emit;e.context=&adapter;
#ifdef XE_TRANSFER_UINT_COLOR_TEST
 p.halves=(uintptr_t)field(&alu,input,base,6);p.wide=(uintptr_t)field(&alu,input,base,7);
 p.shift16=(uintptr_t)integer(m,16);
 for(i=0;i<4;++i)source[i]=(uintptr_t)field(&alu,input,base,i);
 if(!xe_transfer_uint_color_pack(&e,&p,source,packed))goto done;
 values[0]=(XeDxilValue)packed[0];values[1]=(XeDxilValue)packed[1];
 a=dxil_emit_binop(m,DXIL_BINOP_AND,(XeDxilValue)p.wide,integer(m,1),0);
 values[6]=a?dxil_emit_binop(m,DXIL_BINOP_ADD,a,integer(m,1),0):NULL;
 if(!values[6])goto done;
 p.halves=(uintptr_t)field(&alu,input,base,8);p.wide=(uintptr_t)field(&alu,input,base,9);
 packed[0]=(uintptr_t)field(&alu,input,base,4);packed[1]=(uintptr_t)field(&alu,input,base,5);
 if(!xe_transfer_uint_color_unpack(&e,&p,packed,result))goto done;
 for(i=0;i<4;++i)values[i+2]=(XeDxilValue)result[i];
 (void)sizeof(c);
#else
 p.msaa=(uintptr_t)field(&alu,input,base,3);p.native2=(uintptr_t)field(&alu,input,base,4);
 p.scale_x=(uintptr_t)field(&alu,input,base,5);p.scale_y=(uintptr_t)field(&alu,input,base,6);
 p.reciprocal_x=(uintptr_t)field(&alu,input,base,7);p.reciprocal_y=(uintptr_t)field(&alu,input,base,8);
 a=field(&alu,input,base,0);b=field(&alu,input,base,1);c=field(&alu,input,base,2);
 if(!xe_transfer_sample_canonical_emit(&e,&p,(uintptr_t)a,(uintptr_t)b,(uintptr_t)c,&canonical))goto done;
 inverse.x=(uintptr_t)field(&alu,input,base,9);inverse.y=(uintptr_t)field(&alu,input,base,10);
 inverse.sub_x=(uintptr_t)field(&alu,input,base,11);inverse.sub_y=(uintptr_t)field(&alu,input,base,12);inverse.sample=(uintptr_t)integer(m,0);
 p.msaa=(uintptr_t)field(&alu,input,base,13);p.native2=(uintptr_t)field(&alu,input,base,14);
 p.scale_x=(uintptr_t)field(&alu,input,base,15);p.scale_y=(uintptr_t)field(&alu,input,base,16);
 if(!xe_transfer_sample_decanonical_emit(&e,&p,&inverse,&result))goto done;
 values[0]=(XeDxilValue)canonical.x;values[1]=(XeDxilValue)canonical.y;
 values[2]=(XeDxilValue)canonical.sub_x;values[3]=(XeDxilValue)canonical.sub_y;
 values[4]=(XeDxilValue)result.x;values[5]=(XeDxilValue)result.y;values[6]=(XeDxilValue)result.sample;
#endif
 for(i=0;i<7;++i) {
  a=dxil_emit_binop(m,DXIL_BINOP_ADD,dest,integer(m,i*4),0);
  if(!a || !xe_dxil_raw_buffer_store(&alu,output,a,values[i]))goto done;
 }
 if(alu.failed || !dxil_emit_branch(m,NULL,blocks+1,0) || !block(m,blocks+1) ||
    !dxil_emit_ret_void(m) || !xe_dxil_shader_finish(shader,&binary))goto done;
 file=fopen(argv[1],"wb");if(!file)goto done;
 failed=fwrite(binary.data,1,binary.size,file)!=binary.size;
 if(fclose(file))failed=1;
 if(!failed) {
  sprintf(path,"%s.list",argv[1]);file=fopen(path,"wb");
  if(!file)failed=1;
  else {if(fprintf(file,"%s\n",argv[1])<0)failed=1;if(fclose(file))failed=1;}
 }
 printf("Transfer sample DXIL: %lu bytes\n",(unsigned long)binary.size);
done:xe_dxil_shader_binary_free(&binary);xe_dxil_shader_destroy(shader);return failed;
}
