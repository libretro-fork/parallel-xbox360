/* IDs borrow native arena values, without copied payload or intermediate IR. */
#include "transfer_format.h"
#include <dxil_module.h>
#include <dxil_function.h>
int xe_dxil_transfer_format_init(xe_transfer_format_emitter *out,XeDxilAlu *alu) {
 if(!out || !alu || !alu->module || alu->failed)return 0;
 out->emit=xe_dxil_transfer_format_emit;out->context=alu;out->failed=0;return 1;
}
uintptr_t xe_dxil_transfer_format_emit(void *context,enum xe_transfer_format_op op,uintptr_t a,uintptr_t b,uintptr_t c) {
 XeDxilAlu *alu=(XeDxilAlu*)context;struct dxil_module *m;
 XeDxilValue av=(XeDxilValue)a,bv=(XeDxilValue)b,cv=(XeDxilValue)c,value=NULL,args[2];
 const struct dxil_type *u32,*f32;const struct dxil_func *function;
 enum dxil_bin_opcode binary;enum dxil_cmp_pred comparison;enum dxil_cast_opcode cast;
 int floating=0;
 if(!alu || !alu->module || alu->failed)return 0;
 m=alu->module;u32=dxil_module_get_int_type(m,32);f32=dxil_module_get_float_type(m,32);
 if(!u32 || !f32)goto failed;
 switch(op){
  case XE_TF_U32:value=dxil_module_get_int32_const(m,(int32_t)(uint32_t)a);break;
  case XE_TF_F32_BITS:value=xe_dxil_alu_float_bits(alu,(uint32_t)a);break;
  case XE_TF_SELECT_U32:case XE_TF_SELECT_F32:value=a && b && c?dxil_emit_select(m,av,bv,cv):NULL;break;
  case XE_TF_TRUNC_F32:case XE_TF_ROUND_EVEN_F32:
   function=dxil_get_function(m,"dx.op.unary",DXIL_F32);args[0]=dxil_module_get_int32_const(m,op==XE_TF_TRUNC_F32?29:26);args[1]=av;
   value=function && a && args[0]?dxil_emit_call(m,function,args,2):NULL;break;
  case XE_TF_SATURATE_F32:value=a?xe_dxil_alu_saturate(alu,av):NULL;break;
  case XE_TF_CLAMP_POSITIVE_F32:
   if(!a || !b)goto failed;
   value=dxil_module_get_float_const(m,0.0f);
   if(value){
    XeDxilValue positive=dxil_emit_cmp(m,DXIL_FCMP_OGT,av,value);
    value=positive?dxil_emit_select(m,positive,av,value):NULL;
    if(value){positive=dxil_emit_cmp(m,DXIL_FCMP_OLT,value,bv);value=positive?dxil_emit_select(m,positive,value,bv):NULL;}
   }
   break;
  case XE_TF_U32_TO_F32:case XE_TF_F32_TO_U32:case XE_TF_BITCAST_U32_F32:case XE_TF_BITCAST_F32_U32:
   floating=op==XE_TF_U32_TO_F32 || op==XE_TF_BITCAST_U32_F32;
   cast=op==XE_TF_U32_TO_F32?DXIL_CAST_UITOFP:op==XE_TF_F32_TO_U32?DXIL_CAST_FPTOUI:DXIL_CAST_BITCAST;
   value=a?dxil_emit_cast(m,cast,floating?f32:u32,av):NULL;break;
  case XE_TF_EQ_U32:case XE_TF_NE_U32:case XE_TF_LT_U32:case XE_TF_GE_U32:
  case XE_TF_LT_F32:case XE_TF_GT_F32:case XE_TF_GE_F32:
   switch(op){case XE_TF_EQ_U32:comparison=DXIL_ICMP_EQ;break;case XE_TF_NE_U32:comparison=DXIL_ICMP_NE;break;
    case XE_TF_LT_U32:comparison=DXIL_ICMP_ULT;break;case XE_TF_GE_U32:comparison=DXIL_ICMP_UGE;break;
    case XE_TF_LT_F32:comparison=DXIL_FCMP_OLT;break;case XE_TF_GT_F32:comparison=DXIL_FCMP_OGT;break;
    default:comparison=DXIL_FCMP_OGE;break;}
   value=a && b?dxil_emit_cmp(m,comparison,av,bv):NULL;break;
  default:
   switch(op){case XE_TF_ADD_U32:case XE_TF_ADD_F32:binary=DXIL_BINOP_ADD;break;
    case XE_TF_SUB_U32:binary=DXIL_BINOP_SUB;break;case XE_TF_AND_U32:binary=DXIL_BINOP_AND;break;
    case XE_TF_OR_U32:binary=DXIL_BINOP_OR;break;case XE_TF_SHL_U32:binary=DXIL_BINOP_SHL;break;
    case XE_TF_SHR_U32:binary=DXIL_BINOP_LSHR;break;case XE_TF_MUL_F32:binary=DXIL_BINOP_MUL;break;
    default:goto failed;}
   value=a && b?dxil_emit_binop(m,binary,av,bv,0):NULL;break;
 }
 if(value && !alu->failed)return (uintptr_t)value;
failed:alu->failed=1;return 0;
}
