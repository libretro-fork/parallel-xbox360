/* Native raw-word adapter for runtime transfer addressing. BSD; C89. */
#ifndef XE_CORE_TRANSFER_SAMPLE_SPV_H
#define XE_CORE_TRANSFER_SAMPLE_SPV_H
#include "core_transfer_sample.h"
#include "core_spv_binary.h"
#include <spirv/unified1/spirv.h>
typedef struct xe_transfer_sample_spv {
 xe_spv_binary *out;
 uint32_t *next;
 uint32_t uint_type,bool_type,pair_type;
 uint32_t zero,one,two,not_one,not_two,not_three,all,tile_mask;
} xe_transfer_sample_spv;
static INLINE uintptr_t xe_transfer_sample_spv_emit(void *context,
 enum xe_transfer_sample_op op,uintptr_t a,uintptr_t b,uintptr_t c) {
 xe_transfer_sample_spv *s=(xe_transfer_sample_spv*)context;
 uint32_t operands[3],result,opcode,type;
 unsigned count=2;
 if(!s || !s->out || !s->next || s->out->failed)return 0;
 if(op==XE_TRANSFER_SAMPLE_CONST) {
  switch(a) {
   case 0:return s->zero;case 1:return s->one;case 2:return s->two;
   case 0xfffffffeu:return s->not_one;
   case 0xfffffffdu:return s->not_two;
   case 0xfffffffcu:return s->not_three;
   case 0xffffffffu:return s->all;
   case 2047:if(s->tile_mask)return s->tile_mask;
    s->out->failed=1;return 0;
   default:s->out->failed=1;return 0;
  }
 }
 if(!a || !b || a>UINT32_MAX || b>UINT32_MAX || c>UINT32_MAX ||
    *s->next>UINT32_MAX-2) {s->out->failed=1;return 0;}
 operands[0]=(uint32_t)a;operands[1]=(uint32_t)b;operands[2]=(uint32_t)c;
 type=s->uint_type;
 switch(op) {
  case XE_TRANSFER_SAMPLE_MUL_HIGH:opcode=SpvOpUMulExtended;type=s->pair_type;break;
  case XE_TRANSFER_SAMPLE_EQ:opcode=SpvOpIEqual;type=s->bool_type;break;
  case XE_TRANSFER_SAMPLE_MUL:opcode=SpvOpIMul;break;
  case XE_TRANSFER_SAMPLE_SUB:opcode=SpvOpISub;break;
  case XE_TRANSFER_SAMPLE_GE:opcode=SpvOpUGreaterThanEqual;type=s->bool_type;break;
  case XE_TRANSFER_SAMPLE_ADD:opcode=SpvOpIAdd;break;
  case XE_TRANSFER_SAMPLE_SELECT:
   if(!c){s->out->failed=1;return 0;}opcode=SpvOpSelect;count=3;break;
  case XE_TRANSFER_SAMPLE_AND:opcode=SpvOpBitwiseAnd;break;
  case XE_TRANSFER_SAMPLE_OR:opcode=SpvOpBitwiseOr;break;
  case XE_TRANSFER_SAMPLE_XOR:opcode=SpvOpBitwiseXor;break;
  case XE_TRANSFER_SAMPLE_SHR:opcode=SpvOpShiftRightLogical;break;
  case XE_TRANSFER_SAMPLE_SHL:opcode=SpvOpShiftLeftLogical;break;
  default:s->out->failed=1;return 0;
 }
 result=(*s->next)++;
 xe_spv_binary_instruction(s->out,opcode,type,result,operands,count);
 if(op==XE_TRANSFER_SAMPLE_MUL_HIGH) {
  operands[0]=result;operands[1]=1;result=(*s->next)++;
  xe_spv_binary_instruction(s->out,SpvOpCompositeExtract,s->uint_type,result,operands,2);
 }
 return s->out->failed?0:result;
}
#endif
