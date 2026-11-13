/* Direct raw SPIR-V format adapter. BSD; MSVC C89.
 * Borrowed IDs and caller-owned final word storage; no intermediate IR graph.
 * The caller owns declaration/annotation placement and constant interning.
 * A null output plans IDs/annotations before emitting the final module. */
#ifndef XE_CORE_TRANSFER_FORMAT_SPV_H
#define XE_CORE_TRANSFER_FORMAT_SPV_H
#include "core_transfer_format.h"
#include "core_spv_binary.h"
#include <spirv/unified1/spirv.h>

typedef uint32_t (*xe_transfer_format_spv_constant_fn)(void *,unsigned,uint32_t);
typedef int (*xe_transfer_format_spv_precise_fn)(void *,uint32_t);
typedef struct xe_transfer_format_spv {
 xe_spv_binary *out;
 uint32_t *next;
 size_t *word_count;
 uint32_t uint_type,float_type,bool_type,extended_set;
 xe_transfer_format_spv_constant_fn constant;
 xe_transfer_format_spv_precise_fn precise;
 void *owner;
 unsigned failed;
} xe_transfer_format_spv;

static INLINE uintptr_t xe_transfer_format_spv_emit(void *,enum xe_transfer_format_op,uintptr_t,uintptr_t,uintptr_t);

static INLINE uint32_t xe_transfer_format_spv_instruction(xe_transfer_format_spv *s,
 unsigned opcode,uint32_t type,const uint32_t *operands,unsigned count)
{
 uint32_t result;
 if(!s || s->failed || !s->next || !*s->next || *s->next==UINT32_MAX || count>65532 ||
    !type || (s->out && s->out->failed))return 0;
 if(s->word_count){
  size_t words=3+(size_t)count;
  if(*s->word_count>(size_t)-1-words){s->failed=1;return 0;}
  *s->word_count+=words;
 }
 result=(*s->next)++;
 if(s->out)xe_spv_binary_instruction(s->out,opcode,type,result,operands,count);
 if(s->out && s->out->failed){s->failed=1;return 0;}
 return result;
}

static INLINE uintptr_t xe_transfer_format_spv_emit(void *context,
 enum xe_transfer_format_op op,uintptr_t a,uintptr_t b,uintptr_t c)
{
 xe_transfer_format_spv *s=(xe_transfer_format_spv*)context;
 uint32_t operands[3],opcode,type,result,zero,one;
 unsigned count=2,precise=0;
 if(!s || s->failed || !s->constant || !s->next ||
    !s->uint_type || !s->float_type || !s->bool_type)goto failed;
 if(op==XE_TF_U32 || op==XE_TF_F32_BITS){
  if(a>UINT32_MAX)goto failed;
  /* Float literals may quiet signaling NaNs before a later bitcast. Keep
   * exceptional/zero/subnormal payloads as integer constants until the use. */
  if(op==XE_TF_F32_BITS && (!((uint32_t)a&0x7f800000u) ||
       ((uint32_t)a&0x7f800000u)==0x7f800000u)){
   operands[0]=s->constant(s->owner,0,(uint32_t)a);
   if(!operands[0])goto failed;
   result=xe_transfer_format_spv_instruction(s,SpvOpBitcast,s->float_type,operands,1);
  }else result=s->constant(s->owner,op==XE_TF_F32_BITS,(uint32_t)a);
  if(!result)goto failed;
  return result;
 }
 if(!a || a>UINT32_MAX || b>UINT32_MAX || c>UINT32_MAX)goto failed;
 operands[0]=(uint32_t)a;operands[1]=(uint32_t)b;operands[2]=(uint32_t)c;
 type=s->uint_type;
 switch(op){
  case XE_TF_ADD_U32:opcode=SpvOpIAdd;break;
  case XE_TF_SUB_U32:opcode=SpvOpISub;break;
  case XE_TF_AND_U32:opcode=SpvOpBitwiseAnd;break;
  case XE_TF_OR_U32:opcode=SpvOpBitwiseOr;break;
  case XE_TF_SHL_U32:opcode=SpvOpShiftLeftLogical;break;
  case XE_TF_SHR_U32:opcode=SpvOpShiftRightLogical;break;
  case XE_TF_EQ_U32:opcode=SpvOpIEqual;type=s->bool_type;break;
  case XE_TF_NE_U32:opcode=SpvOpINotEqual;type=s->bool_type;break;
  case XE_TF_LT_U32:opcode=SpvOpULessThan;type=s->bool_type;break;
  case XE_TF_GE_U32:opcode=SpvOpUGreaterThanEqual;type=s->bool_type;break;
  case XE_TF_SELECT_U32:case XE_TF_SELECT_F32:
   if(!c)goto failed;
   opcode=SpvOpSelect;count=3;
   if(op==XE_TF_SELECT_F32)type=s->float_type;
   break;
  case XE_TF_ADD_F32:opcode=SpvOpFAdd;type=s->float_type;precise=1;break;
  case XE_TF_MUL_F32:opcode=SpvOpFMul;type=s->float_type;precise=1;break;
  case XE_TF_LT_F32:opcode=SpvOpFOrdLessThan;type=s->bool_type;break;
  case XE_TF_GT_F32:opcode=SpvOpFOrdGreaterThan;type=s->bool_type;break;
  case XE_TF_GE_F32:opcode=SpvOpFOrdGreaterThanEqual;type=s->bool_type;break;
  case XE_TF_U32_TO_F32:opcode=SpvOpConvertUToF;type=s->float_type;count=1;break;
  case XE_TF_F32_TO_U32:opcode=SpvOpConvertFToU;count=1;break;
  case XE_TF_BITCAST_U32_F32:opcode=SpvOpBitcast;type=s->float_type;count=1;break;
  case XE_TF_BITCAST_F32_U32:opcode=SpvOpBitcast;count=1;break;
  case XE_TF_TRUNC_F32:case XE_TF_ROUND_EVEN_F32:
   if(!s->extended_set)goto failed;
   opcode=SpvOpExtInst;type=s->float_type;count=3;
   operands[0]=s->extended_set;operands[1]=op==XE_TF_TRUNC_F32?3:2; /* Trunc/RoundEven; no GLSL source. */
   operands[2]=(uint32_t)a;break;
  case XE_TF_SATURATE_F32:case XE_TF_CLAMP_POSITIVE_F32:
   zero=s->constant(s->owner,1,0);one=s->constant(s->owner,1,0x3f800000);
   if(!zero || !one || !s->extended_set || (op==XE_TF_CLAMP_POSITIVE_F32 && !b))goto failed;
   {uint32_t args[5];args[0]=s->extended_set;args[1]=81; /* NClamp */
    args[2]=(uint32_t)a;args[3]=zero;args[4]=op==XE_TF_SATURATE_F32?one:(uint32_t)b;
    result=xe_transfer_format_spv_instruction(s,SpvOpExtInst,s->float_type,args,5);}
   if(!result)goto failed;
   /* Normalize signed zero explicitly; bitcasts do not perform numeric work. */
   operands[0]=result;result=xe_transfer_format_spv_instruction(s,SpvOpBitcast,s->uint_type,operands,1);
   if(!result)goto failed;
   operands[0]=result;operands[1]=s->constant(s->owner,0,0x7fffffffu);
   if(!operands[1])goto failed;
   result=xe_transfer_format_spv_instruction(s,SpvOpBitwiseAnd,s->uint_type,operands,2);
   if(!result)goto failed;
   operands[0]=result;result=xe_transfer_format_spv_instruction(s,SpvOpBitcast,s->float_type,operands,1);
   if(!result)goto failed;
   return result;
  default:goto failed;
 }
 if(count>=2 && op!=XE_TF_TRUNC_F32 && op!=XE_TF_ROUND_EVEN_F32 && !b)goto failed;
 result=xe_transfer_format_spv_instruction(s,opcode,type,operands,count);
 if(!result || (precise && (!s->precise || !s->precise(s->owner,result))))goto failed;
 return result;
failed:
 if(s){s->failed=1;if(s->out)s->out->failed=1;}
 return 0;
}
#endif
