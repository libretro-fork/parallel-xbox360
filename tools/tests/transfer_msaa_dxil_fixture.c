/* Independent MS sample fill/probe, native DXIL. Test-only BSD; MSVC C89. */
#include "src/xenia/gpu/dxil/guest_resources.h"
#include "src/xenia/gpu/dxil/shader_module.h"
#include <dxil_module.h>
#include <dxil_function.h>
#include <stdio.h>
#include <string.h>
static XeDxilValue integer(struct dxil_module *m,uint32_t n){return dxil_module_get_int32_const(m,(int32_t)n);}
static XeDxilValue shift(struct dxil_module *m,XeDxilValue a,unsigned n){return dxil_emit_binop(m,DXIL_BINOP_SHL,a,integer(m,n),0);}
static XeDxilValue xor2(struct dxil_module *m,XeDxilValue a,XeDxilValue b){return dxil_emit_binop(m,DXIL_BINOP_XOR,a,b,0);}
static int begin(struct dxil_module *m,unsigned block) {
 struct dxil_func_def *f=m->cur_emitting_func;
 if(block>=f->num_basic_block_ids || f->basic_block_ids[block]!=-1)return 0;
 f->basic_block_ids[block]=(int)f->curr_block;return 1;
}
static int build_fill(XeDxilShaderBinary *out) {
 XeDxilShaderModule *shader=xe_dxil_shader_create(DXIL_PIXEL_SHADER);struct dxil_module *m;XeDxilAlu alu;
 const struct dxil_func *function;XeDxilValue position[2],sample,mask,cbv,args[5],color[4];unsigned i;int result=0;
 if(!shader)return 0;m=xe_dxil_shader_module(shader);xe_dxil_alu_init(&alu,m);
 if(xe_dxil_shader_signature(shader,0,"SV_Position",0,DXIL_SEM_POSITION,DXIL_PROG_SEM_POSITION,0,0,4,DXIL_INTERP_LINEAR_NOPERSPECTIVE)!=0 ||
    xe_dxil_shader_signature_typed(shader,1,"SV_Target",0,DXIL_SEM_TARGET,DXIL_PROG_SEM_TARGET,0,0,4,0,DXIL_COMP_TYPE_U32)!=0 ||
    xe_dxil_shader_resource(shader,XE_DXIL_RESOURCE_CBV,DXIL_RESOURCE_KIND_CBUFFER,0,0,1,16)!=0)goto done;
 cbv=xe_dxil_resource_handle(&alu,XE_DXIL_RESOURCE_CBV,0,0);sample=cbv?xe_dxil_cbuffer_word(&alu,cbv,integer(m,0)):NULL;mask=cbv?xe_dxil_cbuffer_word(&alu,cbv,integer(m,1)):NULL;if(!sample || !mask)goto done;
 function=dxil_get_function(m,"dx.op.loadInput",DXIL_F32);args[0]=integer(m,4);args[1]=args[2]=integer(m,0);args[4]=dxil_module_get_undef(m,dxil_module_get_int_type(m,32));
 for(i=0;i<2;++i){args[3]=dxil_module_get_int8_const(m,(int8_t)i);position[i]=function?dxil_emit_call(m,function,args,5):NULL;if(!position[i])goto done;position[i]=dxil_emit_cast(m,DXIL_CAST_FPTOUI,dxil_module_get_int_type(m,32),position[i]);if(!position[i])goto done;}
 color[0]=xor2(m,integer(m,0x7fc00000u),xor2(m,position[0],xor2(m,shift(m,position[1],8),shift(m,sample,20))));
 color[1]=xor2(m,integer(m,0x80008000u),xor2(m,shift(m,position[0],4),xor2(m,position[1],shift(m,sample,12))));
 color[2]=xor2(m,integer(m,0xffff0000u),xor2(m,shift(m,position[0],8),xor2(m,shift(m,position[1],1),shift(m,sample,14))));
 color[3]=xor2(m,integer(m,0x0000ffffu),xor2(m,position[0],xor2(m,shift(m,position[1],8),shift(m,sample,4))));
 function=dxil_get_function(m,"dx.op.storeOutput",DXIL_I32);args[0]=integer(m,5);args[1]=args[2]=integer(m,0);
 for(i=0;i<4;++i){if(!color[i])goto done;color[i]=dxil_emit_binop(m,DXIL_BINOP_AND,color[i],mask,0);args[3]=dxil_module_get_int8_const(m,(int8_t)i);args[4]=color[i];if(!color[i] || !function || !dxil_emit_call_void(m,function,args,5))goto done;}
 if(!alu.failed && dxil_emit_ret_void(m) && xe_dxil_shader_finish(shader,out))result=1;
done:xe_dxil_shader_destroy(shader);return result;
}
static int build_probe(unsigned multisampled,XeDxilShaderBinary *out) {
 XeDxilShaderModule *shader=xe_dxil_shader_create(DXIL_COMPUTE_SHADER);struct dxil_module *m;XeDxilAlu alu;
 const struct dxil_func *function;XeDxilValue cbv,image,output,values[4],args[9],index,a,b,condition,total,pixel,x,y,sample,base,value;unsigned i,blocks;int result=0;
 if(!shader)return 0;m=xe_dxil_shader_module(shader);xe_dxil_alu_init(&alu,m);
 if(!xe_dxil_shader_threads(shader,64,1,1) ||
    xe_dxil_shader_resource_typed(shader,XE_DXIL_RESOURCE_SRV,multisampled?DXIL_RESOURCE_KIND_TEXTURE2DMS:DXIL_RESOURCE_KIND_TEXTURE2D,0,0,1,0,DXIL_COMP_TYPE_U32)!=0 ||
    xe_dxil_shader_resource(shader,XE_DXIL_RESOURCE_UAV,DXIL_RESOURCE_KIND_RAW_BUFFER,0,0,1,0)!=0 ||
    xe_dxil_shader_resource(shader,XE_DXIL_RESOURCE_CBV,DXIL_RESOURCE_KIND_CBUFFER,0,0,1,16)!=0)goto done;
 cbv=xe_dxil_resource_handle(&alu,XE_DXIL_RESOURCE_CBV,0,0);image=xe_dxil_resource_handle(&alu,XE_DXIL_RESOURCE_SRV,0,0);output=xe_dxil_resource_handle(&alu,XE_DXIL_RESOURCE_UAV,0,0);if(!cbv || !image || !output)goto done;
 for(i=0;i<4;++i){values[i]=xe_dxil_cbuffer_word(&alu,cbv,integer(m,i));if(!values[i])goto done;}
 function=dxil_get_function(m,"dx.op.threadId",DXIL_I32);args[0]=integer(m,93);args[1]=integer(m,0);index=function?dxil_emit_call(m,function,args,2):NULL;if(!index)goto done;
 total=dxil_emit_binop(m,DXIL_BINOP_MUL,values[0],values[1],0);total=total?dxil_emit_binop(m,DXIL_BINOP_MUL,total,values[2],0):NULL;if(!total)goto done;
 a=dxil_emit_cmp(m,DXIL_ICMP_ULT,index,total);b=dxil_emit_cmp(m,DXIL_ICMP_ULT,index,values[3]);condition=a && b?dxil_emit_binop(m,DXIL_BINOP_AND,a,b,0):NULL;
 if(!condition || !dxil_function_reserve_blocks(m,2,&blocks) || !dxil_emit_branch(m,condition,blocks,blocks+1) || !begin(m,blocks))goto done;
 pixel=dxil_emit_binop(m,DXIL_BINOP_UDIV,index,values[2],0);sample=dxil_emit_binop(m,DXIL_BINOP_UREM,index,values[2],0);x=pixel?dxil_emit_binop(m,DXIL_BINOP_UREM,pixel,values[0],0):NULL;y=pixel?dxil_emit_binop(m,DXIL_BINOP_UDIV,pixel,values[0],0):NULL;
 if(!x || !y || !sample)goto done;
 function=dxil_get_function(m,"dx.op.textureLoad",DXIL_I32);args[0]=integer(m,66);args[1]=image;args[2]=multisampled?sample:integer(m,0);args[3]=x;args[4]=y;args[5]=dxil_module_get_undef(m,dxil_module_get_int_type(m,32));args[6]=args[7]=integer(m,0);args[8]=args[5];
 value=function?dxil_emit_call(m,function,args,9):NULL;if(!value)goto done;base=dxil_emit_binop(m,DXIL_BINOP_MUL,index,integer(m,16),0);if(!base)goto done;
 for(i=0;i<4;++i){a=dxil_emit_extractval(m,value,i);b=dxil_emit_binop(m,DXIL_BINOP_ADD,base,integer(m,i*4),0);if(!a || !b || !xe_dxil_raw_buffer_store(&alu,output,b,a))goto done;}
 if(!alu.failed && dxil_emit_branch(m,NULL,blocks+1,0) && begin(m,blocks+1) && dxil_emit_ret_void(m) && xe_dxil_shader_finish(shader,out))result=1;
done:xe_dxil_shader_destroy(shader);return result;
}
int main(int argc,char **argv) {
 XeDxilShaderBinary out={0};FILE *file,*list;char path[4096];unsigned i;int failed;
 if(argc!=2 || strlen(argv[1])>2048)return 1;sprintf(path,"%s-msaa.list",argv[1]);list=fopen(path,"wb");if(!list)return 1;
 for(i=0;i<3;++i) {
  if(!(i==0?build_fill(&out):build_probe(i==2,&out)))goto fail;
  sprintf(path,"%s-%s.dxil",argv[1],i==0?"fill":i==1?"probe-s0":"probe-s1");file=fopen(path,"wb");if(!file)goto fail;
  failed=fwrite(out.data,1,out.size,file)!=out.size;if(fclose(file))failed=1;
  if(failed || fprintf(list,"%s\n",path)<0)goto fail;
  xe_dxil_shader_binary_free(&out);
 }
 return fclose(list)!=0;
fail:xe_dxil_shader_binary_free(&out);fclose(list);return 1;
}
