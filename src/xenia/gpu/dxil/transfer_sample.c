/* IDs borrow native arena values; no copied payload or intermediate IR. */
#include "transfer_sample.h"
#include <dxil_module.h>
#include <dxil_function.h>
int xe_dxil_transfer_sample_init(XeDxilTransferSample *state,XeDxilAlu *alu) {
 const struct dxil_type *parts[2],*pair;
 if(!state || !alu || !alu->module || alu->failed)return 0;
 state->alu=alu;state->multiply=NULL;
 parts[0]=dxil_module_get_int_type(alu->module,32);parts[1]=parts[0];
 if(!parts[0])return 0;
 pair=dxil_module_get_struct_type(alu->module,"dx.types.twoi32",parts,2);
 state->multiply=pair?dxil_alloc_func_with_rettype(alu->module,
  "dx.op.binaryWithTwoOuts",DXIL_I32,pair,"iOO",DXIL_ATTR_KIND_READ_NONE):NULL;
 return state->multiply!=NULL;
}
uintptr_t xe_dxil_transfer_sample_emit(void *context,enum xe_transfer_sample_op op,
 uintptr_t a,uintptr_t b,uintptr_t c) {
 XeDxilTransferSample *s=(XeDxilTransferSample*)context;struct dxil_module *m;
 XeDxilValue av=(XeDxilValue)a,bv=(XeDxilValue)b,cv=(XeDxilValue)c,v=NULL,args[3];
 enum dxil_bin_opcode binary;
 if(!s || !s->alu || s->alu->failed || !s->multiply)return 0;
 m=s->alu->module;
 if(op==XE_TRANSFER_SAMPLE_CONST)v=dxil_module_get_int32_const(m,(int32_t)(uint32_t)a);
 else if(a && b) {
  switch(op) {
   case XE_TRANSFER_SAMPLE_MUL_HIGH:
    args[0]=dxil_module_get_int32_const(m,42);args[1]=av;args[2]=bv;
    v=args[0]?dxil_emit_call(m,s->multiply,args,3):NULL;
    /* DXIL UMul returns high,low; SPIR-V UMulExtended returns low,high. */
    v=v?dxil_emit_extractval(m,v,0):NULL;break;
   case XE_TRANSFER_SAMPLE_EQ:v=dxil_emit_cmp(m,DXIL_ICMP_EQ,av,bv);break;
   case XE_TRANSFER_SAMPLE_GE:v=dxil_emit_cmp(m,DXIL_ICMP_UGE,av,bv);break;
   case XE_TRANSFER_SAMPLE_SELECT:v=c?dxil_emit_select(m,av,bv,cv):NULL;break;
   default:
    switch(op) {
     case XE_TRANSFER_SAMPLE_MUL:binary=DXIL_BINOP_MUL;break;
     case XE_TRANSFER_SAMPLE_SUB:binary=DXIL_BINOP_SUB;break;
     case XE_TRANSFER_SAMPLE_ADD:binary=DXIL_BINOP_ADD;break;
     case XE_TRANSFER_SAMPLE_AND:binary=DXIL_BINOP_AND;break;
     case XE_TRANSFER_SAMPLE_OR:binary=DXIL_BINOP_OR;break;
     case XE_TRANSFER_SAMPLE_XOR:binary=DXIL_BINOP_XOR;break;
     case XE_TRANSFER_SAMPLE_SHR:binary=DXIL_BINOP_LSHR;break;
     case XE_TRANSFER_SAMPLE_SHL:binary=DXIL_BINOP_SHL;break;
     default:s->alu->failed=1;return 0;
    }
    v=dxil_emit_binop(m,binary,av,bv,0);break;
  }
 }
 if(!v)s->alu->failed=1;
 return (uintptr_t)v;
}
