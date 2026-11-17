/* Native CND-only compile-cost fixture. C89; see LICENSE.
 * The first72 literal cases have correct historical AND corrected DXIL output.
 * The full84-case correctness fixture separately retains the12 FTZ failures.
 * This is a compute ALU probe, not a rendering or cold-gameplay measurement. */
#include "guest_alu.h"
#include "dxil_module.h"
#include "dxil_function.h"
#include "shader_conditional_fixture.h"
#include <stdio.h>
#include <string.h>

static int store_word(struct dxil_module *module,XeDxilValue handle,
 unsigned word,XeDxilValue value) {
 XeDxilValue args[9];const struct dxil_func *function;unsigned i;
 function=dxil_get_function(module,"dx.op.bufferStore",DXIL_I32);
 if(!function || !value)return 0;
 args[0]=dxil_module_get_int32_const(module,69);args[1]=handle;
 args[2]=dxil_module_get_int32_const(module,(int32_t)(word*4));
 args[3]=dxil_module_get_undef(module,dxil_module_get_int_type(module,32));
 args[4]=value;args[5]=args[3];args[6]=args[3];args[7]=args[3];
 args[8]=dxil_module_get_int8_const(module,1);
 for(i=0;i<9;++i)if(!args[i])return 0;
 return dxil_emit_call_void(module,function,args,9);
}

/* Same writer entry as dxil_alu_test.c; linked instead of that mixed fixture. */
int xe_dxil_alu_test(struct dxil_module *module,const char *path) {
 char expected_path[4096];FILE *golden;size_t length=strlen(path);
 XeDxilValue args[5],handle,operands[3][4],values[4],value;
 const struct dxil_func *function;XeDxilAlu alu;
 uint32_t input[16],expected[8];unsigned which,source,lane;int result=0;
 if(length>sizeof(expected_path)-10)return 0;
 memcpy(expected_path,path,length);memcpy(expected_path+length,".expected",10);
 golden=fopen(expected_path,"wb");if(!golden)return 0;
 args[0]=dxil_module_get_int32_const(module,57);
 args[1]=dxil_module_get_int8_const(module,DXIL_RESOURCE_CLASS_UAV);
 args[2]=dxil_module_get_int32_const(module,0);args[3]=args[2];
 args[4]=dxil_module_get_int1_const(module,0);
 function=dxil_get_function(module,"dx.op.createHandle",DXIL_NONE);
 if(!function)goto done;
 for(lane=0;lane<5;++lane)if(!args[lane])goto done;
 handle=dxil_emit_call(module,function,args,5);if(!handle)goto done;
 for(which=0;which<72;++which){
  conditional_inputs(input,which);xe_dxil_alu_init(&alu,module);
  if(alu.failed)goto done;
  alu.vertex_round_toward_zero=1;
  for(source=0;source<3;++source)for(lane=0;lane<4;++lane){
   operands[source][lane]=xe_dxil_alu_float_bits(&alu,input[source*4+lane]);
   if(!operands[source][lane])goto done;
  }
  if(!xe_dxil_alu_vector(&alu,XE_DXIL_V_CNDEQ+which%3,operands,0,values) || alu.failed)goto done;
  for(lane=0;lane<8;++lane){
   if(lane<4){
    if(!values[lane])goto done;
    expected[lane]=conditional_golden(which,lane);
    value=dxil_emit_cast(module,DXIL_CAST_BITCAST,
     dxil_module_get_int_type(module,32),values[lane]);
   }else{
    expected[lane]=lane==7?(UINT32_C(0x80000000)|which):0;
    if(lane==4 || lane==6)value=dxil_emit_cast(module,DXIL_CAST_ZEXT,
     dxil_module_get_int_type(module,32),lane==4?alu.predicate:alu.killed);
    else if(lane==5)value=alu.address;
    else value=dxil_module_get_int32_const(module,(int32_t)expected[lane]);
   }
   if(!store_word(module,handle,which*8+lane,value))goto done;
  }
  if(fwrite(expected,sizeof(uint32_t),8,golden)!=8)goto done;
 }
 result=dxil_emit_ret_void(module);
done:
 if(fclose(golden))result=0;
 if(result)puts("DXIL_CONDITIONAL_COST cases=72 non_denormal_conditions=1 raw_payload=1 words=576");
 return result;
}
