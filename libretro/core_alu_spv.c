/* Integer-only binary32 ALU interpreter arithmetic. BSD license. MSVC C89. */
#include "core_alu_spv.h"
#include "core_float_rtz_emit.h"
#include <spirv/unified1/spirv.h>
#include <string.h>
enum {
 T_VOID=1,T_UINT,T_U3,T_U4,T_BOOL,T_B4,T_PAIR,T_ARRAY,T_BUFFER,
 T_PBUFFER,T_PUINT,T_PINPUT,T_MAIN_FN,T_MATH_FN,
 V_INPUT=20,V_OUTPUT,V_INVOCATION,F_ADD,F_MUL,F_MAIN,
 T_REG_ARRAY=32,T_PREG_ARRAY,T_PREG,T_PLOCAL_UINT,V_PROGRAM,V_CONSTANT,C_NULL_REG,
 V_CONTEXT,T_CONTEXT_ARRAY,T_CONTEXT,T_PCONTEXT
};
typedef struct alu_constant { uint32_t literal,scalar,vector; } alu_constant;
typedef struct alu_builder {
 xe_spv_binary *out;
 uint32_t next,block;
 unsigned constants;
 alu_constant constant[64];
} alu_builder;
static uint32_t id(alu_builder *b) { return b->next++; }
static void emit(alu_builder *b,unsigned op,uint32_t type,uint32_t result,
 unsigned n,uint32_t a,uint32_t c,uint32_t d,uint32_t e) {
 uint32_t v[4];if(op==SpvOpLabel)b->block=result;v[0]=a;v[1]=c;v[2]=d;v[3]=e;
 xe_spv_binary_instruction(b->out,op,type,result,v,n);
}
static uint32_t operation(alu_builder *b,unsigned op,uint32_t type,
 uint32_t a,uint32_t c) {
 uint32_t result=id(b);emit(b,op,type,result,2,a,c,0,0);return result;
}
static uint32_t choose(alu_builder *b,uint32_t predicate,uint32_t a,uint32_t c) {
 uint32_t result=id(b);emit(b,SpvOpSelect,T_U4,result,3,predicate,a,c,0);return result;
}
static uint32_t constant(alu_builder *b,uint32_t value,int vector) {
 unsigned i;
 for(i=0;i<b->constants;++i)if(b->constant[i].literal==value)
  return vector?b->constant[i].vector:b->constant[i].scalar;
 b->out->failed=1;return 0;
}
static uint32_t scalar(alu_builder *b,uint32_t value){return constant(b,value,0);}
static uint32_t vector(alu_builder *b,uint32_t value){return constant(b,value,1);}
static xe_rtz_id recipe(void *opaque,unsigned op,xe_rtz_id a,xe_rtz_id c,
 xe_rtz_id d,uint32_t literal) {
 alu_builder *b=(alu_builder*)opaque;uint32_t result,low,high,x,n,part,predicate;
 unsigned i;static const uint32_t thresholds[5]={65536,UINT32_C(0x1000000),UINT32_C(0x10000000),UINT32_C(0x40000000),UINT32_C(0x80000000)};
 static const uint32_t shifts[5]={16,8,4,2,1};
 if(op==XE_RTZ_CONST)return vector(b,literal);
 if(op==XE_RTZ_SELECT)return choose(b,(uint32_t)a,(uint32_t)c,(uint32_t)d);
 if(op==XE_RTZ_CLZ){
  x=(uint32_t)a;n=vector(b,0);
  for(i=0;i<5;++i){
   predicate=operation(b,SpvOpULessThan,T_B4,x,vector(b,thresholds[i]));
   part=choose(b,predicate,vector(b,shifts[i]),vector(b,0));
   n=operation(b,SpvOpIAdd,T_U4,n,part);
   x=operation(b,SpvOpShiftLeftLogical,T_U4,x,part);
  }
  return choose(b,operation(b,SpvOpIEqual,T_B4,(uint32_t)a,vector(b,0)),
   vector(b,32),n);
 }
 if(op==XE_RTZ_MUL_TRUNC16){
  result=operation(b,SpvOpUMulExtended,T_PAIR,(uint32_t)a,(uint32_t)c);
  low=id(b);emit(b,SpvOpCompositeExtract,T_U4,low,2,result,0,0,0);
  high=id(b);emit(b,SpvOpCompositeExtract,T_U4,high,2,result,1,0,0);
  return operation(b,SpvOpBitwiseOr,T_U4,
   operation(b,SpvOpShiftRightLogical,T_U4,low,vector(b,16)),
   operation(b,SpvOpShiftLeftLogical,T_U4,high,vector(b,16)));
 }
 switch(op){
 case XE_RTZ_ADD:return operation(b,SpvOpIAdd,T_U4,(uint32_t)a,(uint32_t)c);
 case XE_RTZ_SUB:return operation(b,SpvOpISub,T_U4,(uint32_t)a,(uint32_t)c);
 case XE_RTZ_MUL:return operation(b,SpvOpIMul,T_U4,(uint32_t)a,(uint32_t)c);
 case XE_RTZ_AND:return operation(b,SpvOpBitwiseAnd,T_U4,(uint32_t)a,(uint32_t)c);
 case XE_RTZ_OR:return operation(b,SpvOpBitwiseOr,T_U4,(uint32_t)a,(uint32_t)c);
 case XE_RTZ_XOR:return operation(b,SpvOpBitwiseXor,T_U4,(uint32_t)a,(uint32_t)c);
 case XE_RTZ_SHL:return operation(b,SpvOpShiftLeftLogical,T_U4,(uint32_t)a,(uint32_t)c);
 case XE_RTZ_SHR:return operation(b,SpvOpShiftRightLogical,T_U4,(uint32_t)a,(uint32_t)c);
 case XE_RTZ_EQ:return operation(b,SpvOpIEqual,T_B4,(uint32_t)a,(uint32_t)c);
 case XE_RTZ_ULT:return operation(b,SpvOpULessThan,T_B4,(uint32_t)a,(uint32_t)c);
 case XE_RTZ_ULE:return operation(b,SpvOpULessThanEqual,T_B4,(uint32_t)a,(uint32_t)c);
 case XE_RTZ_UGE:return operation(b,SpvOpUGreaterThanEqual,T_B4,(uint32_t)a,(uint32_t)c);
 case XE_RTZ_BOOL_AND:return operation(b,SpvOpLogicalAnd,T_B4,(uint32_t)a,(uint32_t)c);
 default:b->out->failed=1;return 0;
 }
}
static void math_function(alu_builder *b,int multiply) {
 xe_rtz_emitter r;uint32_t a=id(b),c=id(b),label=id(b),mag_a,mag_c,nan,inf,sign,special,result;
 emit(b,SpvOpFunction,T_U4,multiply?F_MUL:F_ADD,2,0,T_MATH_FN,0,0);
 emit(b,SpvOpFunctionParameter,T_U4,a,0,0,0,0,0);
 emit(b,SpvOpFunctionParameter,T_U4,c,0,0,0,0,0);
 emit(b,SpvOpLabel,0,label,0,0,0,0,0);
 r.context=b;r.emit=recipe;r.failed=0;
 a=(uint32_t)xe_rtz_emit_flush(&r,a);c=(uint32_t)xe_rtz_emit_flush(&r,c);
 mag_a=operation(b,SpvOpBitwiseAnd,T_U4,a,vector(b,UINT32_C(0x7fffffff)));
 mag_c=operation(b,SpvOpBitwiseAnd,T_U4,c,vector(b,UINT32_C(0x7fffffff)));
 nan=operation(b,SpvOpLogicalOr,T_B4,
  operation(b,SpvOpUGreaterThan,T_B4,mag_a,vector(b,UINT32_C(0x7f800000))),
  operation(b,SpvOpUGreaterThan,T_B4,mag_c,vector(b,UINT32_C(0x7f800000))));
 sign=operation(b,SpvOpBitwiseAnd,T_U4,
  operation(b,SpvOpBitwiseXor,T_U4,a,c),vector(b,UINT32_C(0x80000000)));
 if(multiply){
  inf=operation(b,SpvOpLogicalOr,T_B4,
   operation(b,SpvOpIEqual,T_B4,mag_a,vector(b,UINT32_C(0x7f800000))),
   operation(b,SpvOpIEqual,T_B4,mag_c,vector(b,UINT32_C(0x7f800000))));
  special=choose(b,inf,operation(b,SpvOpBitwiseOr,T_U4,sign,
   vector(b,UINT32_C(0x7f800000))),vector(b,0));
  special=choose(b,nan,vector(b,UINT32_C(0x7fc00000)),special);
  result=(uint32_t)xe_rtz_emit_mul(&r,a,c,special);
  result=choose(b,operation(b,SpvOpLogicalOr,T_B4,
   operation(b,SpvOpIEqual,T_B4,mag_a,vector(b,0)),
   operation(b,SpvOpIEqual,T_B4,mag_c,vector(b,0))),vector(b,0),result);
 }else{
  inf=operation(b,SpvOpLogicalAnd,T_B4,
   operation(b,SpvOpIEqual,T_B4,mag_a,vector(b,UINT32_C(0x7f800000))),
   operation(b,SpvOpIEqual,T_B4,mag_c,vector(b,UINT32_C(0x7f800000))));
  inf=operation(b,SpvOpLogicalAnd,T_B4,inf,
   operation(b,SpvOpINotEqual,T_B4,sign,vector(b,0)));
  special=choose(b,operation(b,SpvOpIEqual,T_B4,mag_a,
   vector(b,UINT32_C(0x7f800000))),a,c);
  special=choose(b,operation(b,SpvOpLogicalOr,T_B4,nan,inf),
   vector(b,UINT32_C(0x7fc00000)),special);
  result=(uint32_t)xe_rtz_emit_add(&r,a,c,special);
 }
 if(r.failed)b->out->failed=1;
 emit(b,SpvOpReturnValue,0,0,1,result,0,0,0);
 emit(b,SpvOpFunctionEnd,0,0,0,0,0,0,0);
}
static uint32_t load_word(alu_builder *b,uint32_t buffer,uint32_t index) {
 uint32_t ptr=id(b),value=id(b);
 emit(b,SpvOpAccessChain,T_PUINT,ptr,3,buffer,scalar(b,0),index,0);
 emit(b,SpvOpLoad,T_UINT,value,1,ptr,0,0,0);return value;
}
static void store_word(alu_builder *b,uint32_t index,uint32_t value) {
 uint32_t ptr=id(b);emit(b,SpvOpAccessChain,T_PUINT,ptr,3,V_OUTPUT,scalar(b,0),index,0);
 emit(b,SpvOpStore,0,0,2,ptr,value,0,0);
}
static uint32_t offset(alu_builder *b,uint32_t base,unsigned word) {
 return operation(b,SpvOpIAdd,T_UINT,base,scalar(b,word));
}
static uint32_t load_vector_from(alu_builder *b,uint32_t buffer,uint32_t base,unsigned first) {
 uint32_t v[4],result=id(b);unsigned i;
 for(i=0;i<4;++i)v[i]=load_word(b,buffer,offset(b,base,first+i));
 xe_spv_binary_instruction(b->out,SpvOpCompositeConstruct,T_U4,result,v,4);return result;
}
static uint32_t load_vector(alu_builder *b,uint32_t base,unsigned first) {
 return load_vector_from(b,V_INPUT,base,first);
}
static uint32_t field(alu_builder *b,uint32_t word,unsigned shift,unsigned mask) {
 if(shift)word=operation(b,SpvOpShiftRightLogical,T_UINT,word,scalar(b,shift));
 return operation(b,SpvOpBitwiseAnd,T_UINT,word,scalar(b,mask));
}
static uint32_t splat(alu_builder *b,uint32_t value) {
 uint32_t result=id(b);emit(b,SpvOpCompositeConstruct,T_U4,result,4,value,value,value,value);return result;
}
static uint32_t modify(alu_builder *b,uint32_t value,uint32_t swizzle,
 uint32_t absolute,uint32_t negative) {
 uint32_t components[4],at,result,mask;unsigned i;
 for(i=0;i<4;++i){
  at=operation(b,SpvOpBitwiseAnd,T_UINT,
   offset(b,field(b,swizzle,i*2,3),i),scalar(b,3));
  components[i]=id(b);emit(b,SpvOpVectorExtractDynamic,T_UINT,components[i],2,value,at,0,0);
 }
 result=id(b);xe_spv_binary_instruction(b->out,SpvOpCompositeConstruct,T_U4,result,components,4);
 mask=operation(b,SpvOpBitwiseXor,T_UINT,
  operation(b,SpvOpShiftLeftLogical,T_UINT,absolute,scalar(b,31)),scalar(b,UINT32_C(0xffffffff)));
 negative=operation(b,SpvOpShiftLeftLogical,T_UINT,negative,scalar(b,31));
 result=operation(b,SpvOpBitwiseAnd,T_U4,result,splat(b,mask));
 return operation(b,SpvOpBitwiseXor,T_U4,result,splat(b,negative));
}
static uint32_t source(alu_builder *b,uint32_t value,unsigned index,
 uint32_t word0,uint32_t word1,uint32_t word2) {
 uint32_t absolute=id(b),is_temp;
 is_temp=operation(b,SpvOpINotEqual,T_BOOL,field(b,word2,31-index,1),scalar(b,0));
 emit(b,SpvOpSelect,T_UINT,absolute,3,is_temp,
  field(b,word2,23-index*8,1),field(b,word0,7,1),0);
 return modify(b,value,field(b,word1,16-index*8,255),absolute,field(b,word1,26-index,1));
}
static uint32_t call(alu_builder *b,uint32_t function,uint32_t a,uint32_t c) {
 uint32_t result=id(b);emit(b,SpvOpFunctionCall,T_U4,result,3,function,a,c,0);return result;
}
static void entry(alu_builder *b) {
 uint32_t start=id(b),finish=id(b),body=id(b),sw_join=id(b),bad=id(b);
 uint32_t add=id(b),mul=id(b),mad=id(b),write=id(b),written=id(b);
 uint32_t invocation,id_x,len_in,len_out,inside,base,instruction,opcode,a,c,d,word0,word1;
 uint32_t add_result,mul_result,mad_result,result,status,value,out_base,op[8];unsigned i;
 emit(b,SpvOpFunction,T_VOID,F_MAIN,2,0,T_MAIN_FN,0,0);
 emit(b,SpvOpLabel,0,start,0,0,0,0,0);
 invocation=id(b);emit(b,SpvOpLoad,T_U3,invocation,1,V_INVOCATION,0,0,0);
 id_x=id(b);emit(b,SpvOpCompositeExtract,T_UINT,id_x,2,invocation,0,0,0);
 len_in=id(b);emit(b,SpvOpArrayLength,T_UINT,len_in,2,V_INPUT,0,0,0);
 len_out=id(b);emit(b,SpvOpArrayLength,T_UINT,len_out,2,V_OUTPUT,0,0,0);
 inside=operation(b,SpvOpLogicalAnd,T_BOOL,
  operation(b,SpvOpULessThan,T_BOOL,id_x,
   operation(b,SpvOpShiftRightLogical,T_UINT,len_in,scalar(b,4))),
  operation(b,SpvOpULessThan,T_BOOL,id_x,
   operation(b,SpvOpShiftRightLogical,T_UINT,len_out,scalar(b,3))));
 emit(b,SpvOpSelectionMerge,0,0,2,finish,0,0,0);
 emit(b,SpvOpBranchConditional,0,0,3,inside,body,finish,0);
 emit(b,SpvOpLabel,0,body,0,0,0,0,0);
 base=operation(b,SpvOpIMul,T_UINT,id_x,scalar(b,16));
 instruction=load_word(b,V_INPUT,offset(b,base,2));
 opcode=operation(b,SpvOpBitwiseAnd,T_UINT,
  operation(b,SpvOpShiftRightLogical,T_UINT,instruction,scalar(b,24)),scalar(b,31));
 word0=load_word(b,V_INPUT,base);word1=load_word(b,V_INPUT,offset(b,base,1));
 a=source(b,load_vector(b,base,3),0,word0,word1,instruction);
 c=source(b,load_vector(b,base,7),1,word0,word1,instruction);
 d=source(b,load_vector(b,base,11),2,word0,word1,instruction);
 emit(b,SpvOpSelectionMerge,0,0,2,sw_join,0,0,0);
 op[0]=opcode;op[1]=bad;op[2]=0;op[3]=add;op[4]=1;op[5]=mul;op[6]=11;op[7]=mad;
 xe_spv_binary_instruction(b->out,SpvOpSwitch,0,0,op,8);
 emit(b,SpvOpLabel,0,add,0,0,0,0,0);add_result=call(b,F_ADD,a,c);
 emit(b,SpvOpBranch,0,0,1,sw_join,0,0,0);
 emit(b,SpvOpLabel,0,mul,0,0,0,0,0);mul_result=call(b,F_MUL,a,c);
 emit(b,SpvOpBranch,0,0,1,sw_join,0,0,0);
 emit(b,SpvOpLabel,0,mad,0,0,0,0,0);mad_result=call(b,F_ADD,call(b,F_MUL,a,c),d);
 emit(b,SpvOpBranch,0,0,1,sw_join,0,0,0);
 emit(b,SpvOpLabel,0,bad,0,0,0,0,0);
 emit(b,SpvOpBranch,0,0,1,sw_join,0,0,0);
 emit(b,SpvOpLabel,0,sw_join,0,0,0,0,0);
 result=id(b);op[0]=add_result;op[1]=add;op[2]=mul_result;op[3]=mul;
 op[4]=mad_result;op[5]=mad;op[6]=vector(b,0);op[7]=bad;
 xe_spv_binary_instruction(b->out,SpvOpPhi,T_U4,result,op,8);
 status=id(b);op[0]=scalar(b,1);op[1]=add;op[2]=scalar(b,1);op[3]=mul;
 op[4]=scalar(b,1);op[5]=mad;op[6]=scalar(b,0);op[7]=bad;
 xe_spv_binary_instruction(b->out,SpvOpPhi,T_UINT,status,op,8);
 out_base=operation(b,SpvOpIMul,T_UINT,id_x,scalar(b,8));
 store_word(b,offset(b,out_base,4),status);
 value=operation(b,SpvOpIEqual,T_BOOL,status,scalar(b,1));
 emit(b,SpvOpSelectionMerge,0,0,2,written,0,0,0);
 emit(b,SpvOpBranchConditional,0,0,3,value,write,written,0);
 emit(b,SpvOpLabel,0,write,0,0,0,0,0);
 for(i=0;i<4;++i){value=id(b);emit(b,SpvOpCompositeExtract,T_UINT,value,2,result,i,0,0);
  store_word(b,offset(b,out_base,i),value);}
 emit(b,SpvOpBranch,0,0,1,written,0,0,0);
 emit(b,SpvOpLabel,0,written,0,0,0,0,0);
 emit(b,SpvOpBranch,0,0,1,finish,0,0,0);
 emit(b,SpvOpLabel,0,finish,0,0,0,0,0);
 emit(b,SpvOpReturn,0,0,0,0,0,0,0);emit(b,SpvOpFunctionEnd,0,0,0,0,0,0,0);
}
/* Packed-program execution helpers. Internal to the C89 raw emitter. */
static uint32_t local_load(alu_builder *b,uint32_t type,uint32_t pointer) {
 uint32_t value=id(b);emit(b,SpvOpLoad,type,value,1,pointer,0,0,0);return value;
}
static void local_store(alu_builder *b,uint32_t pointer,uint32_t value) {
 emit(b,SpvOpStore,0,0,2,pointer,value,0,0);
}
static uint32_t yes(alu_builder *b,uint32_t value) {
 return operation(b,SpvOpINotEqual,T_BOOL,value,scalar(b,0));
}
static uint32_t equal(alu_builder *b,uint32_t a,uint32_t c) {
 return operation(b,SpvOpIEqual,T_BOOL,a,c);
}
static uint32_t both(alu_builder *b,uint32_t a,uint32_t c) {
 return operation(b,SpvOpLogicalAnd,T_BOOL,a,c);
}
static uint32_t either(alu_builder *b,uint32_t a,uint32_t c) {
 return operation(b,SpvOpLogicalOr,T_BOOL,a,c);
}
static uint32_t select_word(alu_builder *b,uint32_t predicate,uint32_t a,uint32_t c) {
 uint32_t value=id(b);emit(b,SpvOpSelect,T_UINT,value,3,predicate,a,c,0);return value;
}
/* Consume either GPU-visible byte order without a host normalization buffer. */
static uint32_t program_word(alu_builder *b,uint32_t at,uint32_t swap) {
 uint32_t raw=load_word(b,V_PROGRAM,at),reversed=scalar(b,0);unsigned i;
 for(i=0;i<4;++i)reversed=operation(b,SpvOpBitwiseOr,T_UINT,reversed,
  operation(b,SpvOpShiftLeftLogical,T_UINT,field(b,raw,i*8,255),scalar(b,(3-i)*8)));
 return select_word(b,operation(b,SpvOpINotEqual,T_BOOL,swap,scalar(b,0)),reversed,raw);
}
static uint32_t array_length(alu_builder *b,uint32_t buffer) {
 uint32_t value=id(b);emit(b,SpvOpArrayLength,T_UINT,value,2,buffer,0,0,0);return value;
}
static uint32_t fits(alu_builder *b,uint32_t first,uint32_t count,uint32_t length) {
 return both(b,operation(b,SpvOpULessThanEqual,T_BOOL,first,length),
  operation(b,SpvOpULessThanEqual,T_BOOL,count,
   operation(b,SpvOpISub,T_UINT,length,first)));
}
/* A failed validation commits no position/register payload. */
static void guard(alu_builder *b,uint32_t predicate,uint32_t output,unsigned status) {
 uint32_t good=id(b),bad=id(b);
 emit(b,SpvOpSelectionMerge,0,0,2,good,0,0,0);
 emit(b,SpvOpBranchConditional,0,0,3,predicate,good,bad,0);
 emit(b,SpvOpLabel,0,bad,0,0,0,0,0);
 if(status)store_word(b,offset(b,output,4),scalar(b,status));
 emit(b,SpvOpReturn,0,0,0,0,0,0,0);
 emit(b,SpvOpLabel,0,good,0,0,0,0,0);
}
static uint32_t reg_pointer(alu_builder *b,uint32_t registers,uint32_t index) {
 uint32_t pointer=id(b);emit(b,SpvOpAccessChain,T_PREG,pointer,2,registers,index,0,0);return pointer;
}
static uint32_t reg_load(alu_builder *b,uint32_t registers,uint32_t index) {
 return local_load(b,T_U4,reg_pointer(b,registers,index));
}
/* Invalid constant windows read zero, matching the CPU reference. No wrapping. */
static uint32_t bank_load(alu_builder *b,uint32_t offset_words,uint32_t count,
 uint32_t window_base,uint32_t window_size,uint32_t index) {
 uint32_t valid,good=id(b),bad=id(b),joined=id(b),value,result=id(b),ops[4];
 valid=both(b,operation(b,SpvOpULessThanEqual,T_BOOL,index,window_size),
  both(b,operation(b,SpvOpULessThan,T_BOOL,window_base,count),
   operation(b,SpvOpULessThan,T_BOOL,index,operation(b,SpvOpISub,T_UINT,count,window_base))));
 emit(b,SpvOpSelectionMerge,0,0,2,joined,0,0,0);
 emit(b,SpvOpBranchConditional,0,0,3,valid,good,bad,0);
 emit(b,SpvOpLabel,0,good,0,0,0,0,0);
 value=load_vector_from(b,V_CONSTANT,operation(b,SpvOpIAdd,T_UINT,offset_words,
  operation(b,SpvOpIMul,T_UINT,operation(b,SpvOpIAdd,T_UINT,window_base,index),scalar(b,4))),0);
 emit(b,SpvOpBranch,0,0,1,joined,0,0,0);
 emit(b,SpvOpLabel,0,bad,0,0,0,0,0);emit(b,SpvOpBranch,0,0,1,joined,0,0,0);
 emit(b,SpvOpLabel,0,joined,0,0,0,0,0);
 ops[0]=value;ops[1]=good;ops[2]=vector(b,0);ops[3]=bad;
 xe_spv_binary_instruction(b->out,SpvOpPhi,T_U4,result,ops,4);return result;
}
static uint32_t program_source(alu_builder *b,uint32_t registers,uint32_t constants[4],
 unsigned index,uint32_t word0,uint32_t word1,uint32_t word2,uint32_t loop_address,uint32_t output) {
 uint32_t at=field(b,word2,16-index*8,255),temporary,constant_value,result=id(b),ops[4];
 uint32_t temp=id(b),con=id(b),joined=id(b),con_end,temp_end;
 uint32_t is_temp=yes(b,field(b,word2,31-index,1));
 emit(b,SpvOpSelectionMerge,0,0,2,joined,0,0,0);
 emit(b,SpvOpBranchConditional,0,0,3,is_temp,temp,con,0);
 emit(b,SpvOpLabel,0,temp,0,0,0,0,0);
 temporary=operation(b,SpvOpBitwiseAnd,T_UINT,at,scalar(b,63));
 if(loop_address){
  /* The masked direct index is already safe; only relative reads need aL and
   * the adjusted-index guard. Keep their loads out of the direct path. */
  uint32_t direct=b->block,relative=id(b),merged=id(b),address=id(b),current,relative_end,adjusted;
  uint32_t is_relative=yes(b,field(b,at,6,1));
  emit(b,SpvOpSelectionMerge,0,0,2,merged,0,0,0);
  emit(b,SpvOpBranchConditional,0,0,3,is_relative,relative,merged,0);
  emit(b,SpvOpLabel,0,relative,0,0,0,0,0);current=local_load(b,T_U4,loop_address);
  emit(b,SpvOpCompositeExtract,T_UINT,address,2,current,0,0,0);
  adjusted=operation(b,SpvOpIAdd,T_UINT,temporary,address);
  guard(b,operation(b,SpvOpULessThan,T_BOOL,adjusted,scalar(b,64)),output,3);
  relative_end=b->block;emit(b,SpvOpBranch,0,0,1,merged,0,0,0);
  emit(b,SpvOpLabel,0,merged,0,0,0,0,0);address=id(b);
  ops[0]=temporary;ops[1]=direct;ops[2]=adjusted;ops[3]=relative_end;
  xe_spv_binary_instruction(b->out,SpvOpPhi,T_UINT,address,ops,4);temporary=address;
 }
 temporary=reg_load(b,registers,temporary);
 temp_end=b->block;
 emit(b,SpvOpBranch,0,0,1,joined,0,0,0);
 emit(b,SpvOpLabel,0,con,0,0,0,0,0);
 constant_value=bank_load(b,constants[0],constants[1],constants[2],constants[3],at);
 con_end=b->block;emit(b,SpvOpBranch,0,0,1,joined,0,0,0);
 emit(b,SpvOpLabel,0,joined,0,0,0,0,0);
 ops[0]=temporary;ops[1]=temp_end;ops[2]=constant_value;ops[3]=con_end;
 xe_spv_binary_instruction(b->out,SpvOpPhi,T_U4,result,ops,4);
 return source(b,result,index,word0,word1,word2);
}
static uint32_t vector_alu(alu_builder *b,uint32_t opcode,uint32_t a,uint32_t c,uint32_t d) {
 uint32_t add=id(b),mul=id(b),mad=id(b),joined=id(b),av,mv,dv,result=id(b),ops[6];
 emit(b,SpvOpSelectionMerge,0,0,2,joined,0,0,0);
 ops[0]=opcode;ops[1]=add;ops[2]=1;ops[3]=mul;ops[4]=11;ops[5]=mad;
 xe_spv_binary_instruction(b->out,SpvOpSwitch,0,0,ops,6);
 emit(b,SpvOpLabel,0,add,0,0,0,0,0);av=call(b,F_ADD,a,c);emit(b,SpvOpBranch,0,0,1,joined,0,0,0);
 emit(b,SpvOpLabel,0,mul,0,0,0,0,0);mv=call(b,F_MUL,a,c);emit(b,SpvOpBranch,0,0,1,joined,0,0,0);
 emit(b,SpvOpLabel,0,mad,0,0,0,0,0);dv=call(b,F_ADD,call(b,F_MUL,a,c),d);emit(b,SpvOpBranch,0,0,1,joined,0,0,0);
 emit(b,SpvOpLabel,0,joined,0,0,0,0,0);
 ops[0]=av;ops[1]=add;ops[2]=mv;ops[3]=mul;ops[4]=dv;ops[5]=mad;
 xe_spv_binary_instruction(b->out,SpvOpPhi,T_U4,result,ops,6);return result;
}
static uint32_t lane_splat(alu_builder *b,uint32_t value,uint32_t component) {
 uint32_t word=id(b);emit(b,SpvOpVectorExtractDynamic,T_UINT,word,2,value,component,0,0);return splat(b,word);
}
static uint32_t scalar_alu(alu_builder *b,uint32_t opcode,uint32_t vector_opcode,
 uint32_t d,uint32_t previous,uint32_t registers,uint32_t constants[4],
 uint32_t word0,uint32_t word1,uint32_t word2) {
 uint32_t add=id(b),con=id(b),retain=id(b),joined=id(b),av,cv,rv,result=id(b),con_end,ops[6];
 uint32_t first,second,swizzle,absolute,negate,at;
 emit(b,SpvOpSelectionMerge,0,0,2,joined,0,0,0);
 ops[0]=opcode;ops[1]=retain;ops[2]=0;ops[3]=add;ops[4]=44;ops[5]=con;
 xe_spv_binary_instruction(b->out,SpvOpSwitch,0,0,ops,6);
 emit(b,SpvOpLabel,0,add,0,0,0,0,0);
 av=call(b,F_ADD,lane_splat(b,d,scalar(b,3)),lane_splat(b,d,
  select_word(b,equal(b,vector_opcode,scalar(b,11)),scalar(b,2),scalar(b,0))));
 emit(b,SpvOpBranch,0,0,1,joined,0,0,0);
 emit(b,SpvOpLabel,0,con,0,0,0,0,0);
 swizzle=field(b,word1,0,255);absolute=field(b,word0,7,1);negate=field(b,word1,24,1);
 first=bank_load(b,constants[0],constants[1],constants[2],constants[3],field(b,word2,0,255));
 first=modify(b,first,swizzle,absolute,negate);
 at=operation(b,SpvOpBitwiseOr,T_UINT,
  operation(b,SpvOpShiftLeftLogical,T_UINT,field(b,word2,29,1),scalar(b,1)),
  operation(b,SpvOpShiftLeftLogical,T_UINT,field(b,word1,2,15),scalar(b,2)));
 second=modify(b,reg_load(b,registers,at),swizzle,absolute,negate);
 cv=call(b,F_ADD,lane_splat(b,first,scalar(b,3)),lane_splat(b,second,scalar(b,0)));
 con_end=b->block;emit(b,SpvOpBranch,0,0,1,joined,0,0,0);
 emit(b,SpvOpLabel,0,retain,0,0,0,0,0);rv=splat(b,local_load(b,T_UINT,previous));
 emit(b,SpvOpBranch,0,0,1,joined,0,0,0);
 emit(b,SpvOpLabel,0,joined,0,0,0,0,0);
 ops[0]=av;ops[1]=add;ops[2]=cv;ops[3]=con_end;ops[4]=rv;ops[5]=retain;
 xe_spv_binary_instruction(b->out,SpvOpPhi,T_U4,result,ops,6);
 at=id(b);emit(b,SpvOpCompositeExtract,T_UINT,at,2,result,0,0,0);local_store(b,previous,at);
 return result;
}
static uint32_t mask_predicate(alu_builder *b,uint32_t mask) {
 uint32_t predicates[4],result=id(b);unsigned lane;
 for(lane=0;lane<4;++lane)predicates[lane]=yes(b,field(b,mask,lane,1));
 xe_spv_binary_instruction(b->out,SpvOpCompositeConstruct,T_B4,result,predicates,4);return result;
}
static void register_write(alu_builder *b,uint32_t registers,uint32_t at,uint32_t mask,uint32_t value) {
 uint32_t write=id(b),joined=id(b),pointer,old,predicate=yes(b,mask);
 emit(b,SpvOpSelectionMerge,0,0,2,joined,0,0,0);
 emit(b,SpvOpBranchConditional,0,0,3,predicate,write,joined,0);
 emit(b,SpvOpLabel,0,write,0,0,0,0,0);
 pointer=reg_pointer(b,registers,at);old=local_load(b,T_U4,pointer);
 local_store(b,pointer,choose(b,mask_predicate(b,mask),value,old));
 emit(b,SpvOpBranch,0,0,1,joined,0,0,0);emit(b,SpvOpLabel,0,joined,0,0,0,0,0);
}
static uint32_t context_word(alu_builder *b,uint32_t word) {
 uint32_t pointer=id(b);
 emit(b,SpvOpAccessChain,T_PUINT,pointer,4,V_CONTEXT,scalar(b,0),
  operation(b,SpvOpShiftRightLogical,T_UINT,word,scalar(b,2)),field(b,word,0,3));
 return local_load(b,T_UINT,pointer);
}
static uint32_t bool_condition(alu_builder *b,uint32_t cf1) {
 uint32_t index=field(b,cf1,2,255),value;
 value=context_word(b,operation(b,SpvOpShiftRightLogical,T_UINT,index,scalar(b,5)));
 value=operation(b,SpvOpBitwiseAnd,T_UINT,
  operation(b,SpvOpShiftRightLogical,T_UINT,value,field(b,index,0,31)),scalar(b,1));
 return equal(b,value,field(b,cf1,10,1));
}
static uint32_t stack_shift(alu_builder *b,uint32_t stack,uint32_t top,int pop) {
 uint32_t ops[6],shifted=id(b),result=id(b);
 ops[0]=stack;ops[1]=stack;ops[2]=pop?1:0;ops[3]=pop?2:0;ops[4]=pop?3:1;ops[5]=pop?3:2;
 xe_spv_binary_instruction(b->out,SpvOpVectorShuffle,T_U4,shifted,ops,6);
 emit(b,SpvOpCompositeInsert,T_U4,result,3,top,shifted,pop?3:0,0);return result;
}
/* Unconditional transfers enter their case directly; only conditional paths
 * read the immutable context bank. An untaken transfer exits the switch before
 * stack/target checks, so a full stack or invalid skipped target is harmless. */
static void transfer_gate(alu_builder *b,uint32_t cf0,uint32_t cf1,uint32_t output,uint32_t skip) {
 uint32_t conditional=id(b),passed=id(b),taken=id(b),unconditional,condition;
 unconditional=yes(b,field(b,cf0,13,1));
 emit(b,SpvOpSelectionMerge,0,0,2,taken,0,0,0);
 emit(b,SpvOpBranchConditional,0,0,3,unconditional,taken,conditional,0);
 emit(b,SpvOpLabel,0,conditional,0,0,0,0,0);
 guard(b,equal(b,field(b,cf0,14,1),scalar(b,0)),output,3);condition=bool_condition(b,cf1);
 emit(b,SpvOpSelectionMerge,0,0,2,passed,0,0,0);
 emit(b,SpvOpBranchConditional,0,0,3,condition,passed,skip,0);
 emit(b,SpvOpLabel,0,passed,0,0,0,0,0);emit(b,SpvOpBranch,0,0,1,taken,0,0,0);
 emit(b,SpvOpLabel,0,taken,0,0,0,0,0);
}
/* Fixed per-invocation PC, return stack and work budget. Leave the build point
 * at the EXEC case; every other supported case rejoins the outer continuation. */
static void control_dispatch(alu_builder *b,uint32_t flow[12],uint32_t base,
 uint32_t swap,uint32_t available,uint32_t output,uint32_t *cf0,uint32_t *cf1) {
 uint32_t body=id(b),exec=id(b),skip=id(b),call_block=id(b),ret=id(b),jump=id(b),bad=id(b),joined=id(b);
 uint32_t pc,steps,pair,at,w0,w1,w2,odd,depth,stack,next,inserted,result,valid,ops[24];
 uint32_t conditional=id(b),taken=id(b),condition;
 uint32_t loop_start=id(b),loop_end=id(b),finished=id(b),repeat=id(b),loop_join=id(b);
 uint32_t loop_constant,count,counts,addresses,address,new_count,step;
 flow[4]=id(b);flow[5]=id(b);flow[6]=id(b);
 emit(b,SpvOpBranch,0,0,1,flow[4],0,0,0);
 emit(b,SpvOpLabel,0,flow[4],0,0,0,0,0);
 emit(b,SpvOpLoopMerge,0,0,3,flow[6],flow[5],0,0);
 emit(b,SpvOpBranch,0,0,1,body,0,0,0);
 emit(b,SpvOpLabel,0,body,0,0,0,0,0);
 steps=local_load(b,T_UINT,flow[3]);
 guard(b,operation(b,SpvOpULessThan,T_BOOL,steps,scalar(b,4096)),output,5);
 local_store(b,flow[3],offset(b,steps,1));pc=local_load(b,T_UINT,flow[0]);
 pair=operation(b,SpvOpShiftRightLogical,T_UINT,pc,scalar(b,1));
 guard(b,operation(b,SpvOpULessThan,T_BOOL,pair,available),output,2);
 at=operation(b,SpvOpIAdd,T_UINT,base,operation(b,SpvOpIMul,T_UINT,pair,scalar(b,3)));
 w0=program_word(b,at,swap);w1=program_word(b,offset(b,at,1),swap);w2=program_word(b,offset(b,at,2),swap);
 odd=yes(b,field(b,pc,0,1));
 *cf0=select_word(b,odd,operation(b,SpvOpBitwiseOr,T_UINT,
  operation(b,SpvOpShiftRightLogical,T_UINT,w1,scalar(b,16)),
  operation(b,SpvOpShiftLeftLogical,T_UINT,w2,scalar(b,16))),w0);
 *cf1=select_word(b,odd,operation(b,SpvOpShiftRightLogical,T_UINT,w2,scalar(b,16)),field(b,w1,0,65535));
 flow[7]=field(b,*cf1,12,15);next=offset(b,pc,1);local_store(b,flow[0],next);
 emit(b,SpvOpSelectionMerge,0,0,2,joined,0,0,0);
 ops[0]=flow[7];ops[1]=bad;ops[2]=0;ops[3]=skip;ops[4]=3;ops[5]=conditional;
 ops[6]=4;ops[7]=conditional;ops[8]=1;ops[9]=exec;ops[10]=2;ops[11]=exec;
 ops[12]=9;ops[13]=call_block;ops[14]=10;ops[15]=ret;ops[16]=11;ops[17]=jump;
 ops[18]=7;ops[19]=loop_start;ops[20]=8;ops[21]=loop_end;
 xe_spv_binary_instruction(b->out,SpvOpSwitch,0,0,ops,22);
 emit(b,SpvOpLabel,0,bad,0,0,0,0,0);store_word(b,offset(b,output,4),scalar(b,3));
 emit(b,SpvOpReturn,0,0,0,0,0,0,0);
 emit(b,SpvOpLabel,0,skip,0,0,0,0,0);emit(b,SpvOpBranch,0,0,1,joined,0,0,0);
 emit(b,SpvOpLabel,0,call_block,0,0,0,0,0);
 transfer_gate(b,*cf0,*cf1,output,joined);depth=local_load(b,T_UINT,flow[1]);
 guard(b,operation(b,SpvOpULessThan,T_BOOL,depth,scalar(b,4)),output,3);
 stack=local_load(b,T_U4,flow[2]);inserted=id(b);
 emit(b,SpvOpVectorInsertDynamic,T_U4,inserted,3,stack,next,depth,0);
 local_store(b,flow[2],inserted);local_store(b,flow[1],offset(b,depth,1));
 local_store(b,flow[0],field(b,*cf0,0,8191));emit(b,SpvOpBranch,0,0,1,joined,0,0,0);
 emit(b,SpvOpLabel,0,loop_start,0,0,0,0,0);
 depth=local_load(b,T_UINT,flow[9]);guard(b,operation(b,SpvOpULessThan,T_BOOL,depth,scalar(b,4)),output,3);
 loop_constant=context_word(b,offset(b,field(b,*cf0,16,31),8));count=field(b,loop_constant,0,255);
 /* Zero-count stack effects need hardware proof; do not pretend to render them. */
 guard(b,yes(b,count),output,3);counts=local_load(b,T_U4,flow[10]);addresses=local_load(b,T_U4,flow[11]);
 address=id(b);emit(b,SpvOpCompositeExtract,T_UINT,address,2,addresses,0,0,0);
 address=select_word(b,yes(b,field(b,*cf0,13,1)),address,field(b,loop_constant,8,255));
 local_store(b,flow[10],stack_shift(b,counts,count,0));local_store(b,flow[11],stack_shift(b,addresses,address,0));
 local_store(b,flow[9],offset(b,depth,1));emit(b,SpvOpBranch,0,0,1,joined,0,0,0);
 emit(b,SpvOpLabel,0,loop_end,0,0,0,0,0);depth=local_load(b,T_UINT,flow[9]);guard(b,yes(b,depth),output,2);
 guard(b,equal(b,field(b,*cf0,21,1),scalar(b,0)),output,3);
 counts=local_load(b,T_U4,flow[10]);addresses=local_load(b,T_U4,flow[11]);count=id(b);
 emit(b,SpvOpCompositeExtract,T_UINT,count,2,counts,0,0,0);new_count=operation(b,SpvOpISub,T_UINT,count,scalar(b,1));
 condition=equal(b,new_count,scalar(b,0));
 emit(b,SpvOpSelectionMerge,0,0,2,loop_join,0,0,0);
 emit(b,SpvOpBranchConditional,0,0,3,condition,finished,repeat,0);
 emit(b,SpvOpLabel,0,finished,0,0,0,0,0);
 local_store(b,flow[9],operation(b,SpvOpISub,T_UINT,depth,scalar(b,1)));
 local_store(b,flow[10],stack_shift(b,counts,scalar(b,0),1));local_store(b,flow[11],stack_shift(b,addresses,scalar(b,0),1));
 emit(b,SpvOpBranch,0,0,1,loop_join,0,0,0);
 emit(b,SpvOpLabel,0,repeat,0,0,0,0,0);inserted=id(b);
 emit(b,SpvOpCompositeInsert,T_U4,inserted,3,new_count,counts,0,0);local_store(b,flow[10],inserted);
 loop_constant=context_word(b,offset(b,field(b,*cf0,16,31),8));
 step=operation(b,SpvOpShiftRightArithmetic,T_UINT,operation(b,SpvOpShiftLeftLogical,T_UINT,loop_constant,scalar(b,8)),scalar(b,24));
 address=id(b);emit(b,SpvOpCompositeExtract,T_UINT,address,2,addresses,0,0,0);address=operation(b,SpvOpIAdd,T_UINT,address,step);
 inserted=id(b);emit(b,SpvOpCompositeInsert,T_U4,inserted,3,address,addresses,0,0);local_store(b,flow[11],inserted);
 local_store(b,flow[0],field(b,*cf0,0,8191));emit(b,SpvOpBranch,0,0,1,loop_join,0,0,0);
 emit(b,SpvOpLabel,0,loop_join,0,0,0,0,0);emit(b,SpvOpBranch,0,0,1,joined,0,0,0);
 emit(b,SpvOpLabel,0,ret,0,0,0,0,0);depth=local_load(b,T_UINT,flow[1]);valid=yes(b,depth);
 at=select_word(b,valid,operation(b,SpvOpISub,T_UINT,depth,scalar(b,1)),scalar(b,0));
 stack=local_load(b,T_U4,flow[2]);result=id(b);emit(b,SpvOpVectorExtractDynamic,T_UINT,result,2,stack,at,0,0);
 local_store(b,flow[1],at);local_store(b,flow[0],select_word(b,valid,result,next));
 emit(b,SpvOpBranch,0,0,1,joined,0,0,0);
 emit(b,SpvOpLabel,0,jump,0,0,0,0,0);transfer_gate(b,*cf0,*cf1,output,joined);
 local_store(b,flow[0],field(b,*cf0,0,8191));emit(b,SpvOpBranch,0,0,1,joined,0,0,0);
 emit(b,SpvOpLabel,0,conditional,0,0,0,0,0);condition=bool_condition(b,*cf1);
 emit(b,SpvOpSelectionMerge,0,0,2,taken,0,0,0);
 emit(b,SpvOpBranchConditional,0,0,3,condition,taken,joined,0);
 emit(b,SpvOpLabel,0,taken,0,0,0,0,0);emit(b,SpvOpBranch,0,0,1,exec,0,0,0);
 /* Keep the switch merge for the EXEC tail, which may exit the outer loop. */
 /* The caller emits the merge after the EXEC tail. */
 flow[8]=joined;
 emit(b,SpvOpLabel,0,exec,0,0,0,0,0);
}
static void program_entry(alu_builder *b,int control) {
 uint32_t start=id(b),registers=id(b),previous=id(b),position=id(b),exports=id(b),written=id(b),iterator=id(b);
 uint32_t invocation,x,length,output_length,descriptor_count,record,output,desc[8],constants[4],swap;
 uint32_t h,body,advance,end,index,predicate,cf0,cf1,count,address,sequence,available,word0,word1,word2;
 uint32_t opcode,scalar_opcode,mask_v,mask_s,is_export,valid,a,c,d,value,scalar_value,at;
 uint32_t store_export,store_temp,stored,overlap,union_mask,zero_mask;
 uint32_t flow[12];
 unsigned i;
 cf0=cf1=count=address=sequence=0;
 emit(b,SpvOpFunction,T_VOID,F_MAIN,2,0,T_MAIN_FN,0,0);
 emit(b,SpvOpLabel,0,start,0,0,0,0,0);
 emit(b,SpvOpVariable,T_PREG_ARRAY,registers,2,SpvStorageClassFunction,C_NULL_REG,0,0);
 emit(b,SpvOpVariable,T_PLOCAL_UINT,previous,2,SpvStorageClassFunction,scalar(b,0),0,0);
 emit(b,SpvOpVariable,T_PREG,position,2,SpvStorageClassFunction,vector(b,0),0,0);
 emit(b,SpvOpVariable,T_PLOCAL_UINT,exports,2,SpvStorageClassFunction,scalar(b,0),0,0);
 emit(b,SpvOpVariable,T_PLOCAL_UINT,written,2,SpvStorageClassFunction,scalar(b,0),0,0);
 emit(b,SpvOpVariable,T_PLOCAL_UINT,iterator,2,SpvStorageClassFunction,scalar(b,0),0,0);
 if(control){
  for(i=0;i<4;++i)flow[i]=id(b);
  emit(b,SpvOpVariable,T_PLOCAL_UINT,flow[0],2,SpvStorageClassFunction,scalar(b,0),0,0);
  emit(b,SpvOpVariable,T_PLOCAL_UINT,flow[1],2,SpvStorageClassFunction,scalar(b,0),0,0);
  emit(b,SpvOpVariable,T_PREG,flow[2],2,SpvStorageClassFunction,vector(b,0),0,0);
  emit(b,SpvOpVariable,T_PLOCAL_UINT,flow[3],2,SpvStorageClassFunction,scalar(b,0),0,0);
  for(i=9;i<12;++i)flow[i]=id(b);
  emit(b,SpvOpVariable,T_PLOCAL_UINT,flow[9],2,SpvStorageClassFunction,scalar(b,0),0,0);
  emit(b,SpvOpVariable,T_PREG,flow[10],2,SpvStorageClassFunction,vector(b,0),0,0);
  emit(b,SpvOpVariable,T_PREG,flow[11],2,SpvStorageClassFunction,vector(b,0),0,0);
 }
 invocation=local_load(b,T_U3,V_INVOCATION);x=id(b);emit(b,SpvOpCompositeExtract,T_UINT,x,2,invocation,0,0,0);
 length=array_length(b,V_INPUT);output_length=array_length(b,V_OUTPUT);
 guard(b,yes(b,length),0,0);descriptor_count=load_word(b,V_INPUT,scalar(b,0));
 valid=both(b,operation(b,SpvOpULessThan,T_BOOL,x,descriptor_count),
  both(b,operation(b,SpvOpULessThan,T_BOOL,x,operation(b,SpvOpShiftRightLogical,T_UINT,output_length,scalar(b,3))),
   operation(b,SpvOpULessThanEqual,T_BOOL,descriptor_count,
    operation(b,SpvOpShiftRightLogical,T_UINT,operation(b,SpvOpISub,T_UINT,length,scalar(b,1)),scalar(b,3)))));
 guard(b,valid,0,0);
 record=offset(b,operation(b,SpvOpIMul,T_UINT,x,scalar(b,8)),1);
 output=operation(b,SpvOpIMul,T_UINT,x,scalar(b,8));
 for(i=0;i<8;++i)desc[i]=load_word(b,V_INPUT,offset(b,record,i));
 swap=field(b,desc[1],31,1);desc[1]=field(b,desc[1],0,0x7fffffff);
 valid=both(b,operation(b,SpvOpULessThanEqual,T_BOOL,desc[3],scalar(b,64)),
  both(b,operation(b,SpvOpULessThanEqual,T_BOOL,desc[5],scalar(b,512)),
   both(b,fits(b,desc[0],desc[1],array_length(b,V_PROGRAM)),
    both(b,fits(b,desc[2],operation(b,SpvOpIMul,T_UINT,desc[3],scalar(b,4)),length),
     fits(b,desc[4],operation(b,SpvOpIMul,T_UINT,desc[5],scalar(b,4)),array_length(b,V_CONSTANT))))));
 valid=both(b,valid,operation(b,SpvOpUGreaterThanEqual,T_BOOL,desc[1],scalar(b,3)));
 guard(b,valid,output,2);
 if(!control){
 cf0=program_word(b,desc[0],swap);cf1=program_word(b,offset(b,desc[0],1),swap);
 count=field(b,cf0,12,7);address=field(b,cf0,0,4095);sequence=field(b,cf0,16,4095);
 available=operation(b,SpvOpUDiv,T_UINT,desc[1],scalar(b,3));
 valid=either(b,equal(b,count,scalar(b,0)),fits(b,address,count,available));guard(b,valid,output,2);
 predicate=equal(b,field(b,cf1,12,15),scalar(b,2));
 at=operation(b,SpvOpISub,T_UINT,
  operation(b,SpvOpShiftLeftLogical,T_UINT,scalar(b,1),operation(b,SpvOpIMul,T_UINT,count,scalar(b,2))),scalar(b,1));
 predicate=both(b,predicate,equal(b,operation(b,SpvOpBitwiseAnd,T_UINT,
  operation(b,SpvOpBitwiseAnd,T_UINT,sequence,at),scalar(b,0x555)),scalar(b,0)));
 predicate=both(b,predicate,operation(b,SpvOpULessThanEqual,T_BOOL,count,scalar(b,6)));
 guard(b,predicate,output,3);
 }else available=operation(b,SpvOpUDiv,T_UINT,desc[1],scalar(b,3));
 for(i=0;i<4;++i)constants[i]=desc[i+4];
 /* Initialize only the supplied input registers; remaining locals start zero. */
 h=id(b);body=id(b);advance=id(b);end=id(b);emit(b,SpvOpBranch,0,0,1,h,0,0,0);
 emit(b,SpvOpLabel,0,h,0,0,0,0,0);index=local_load(b,T_UINT,iterator);
 predicate=operation(b,SpvOpULessThan,T_BOOL,index,desc[3]);
 emit(b,SpvOpLoopMerge,0,0,3,end,advance,0,0);emit(b,SpvOpBranchConditional,0,0,3,predicate,body,end,0);
 emit(b,SpvOpLabel,0,body,0,0,0,0,0);
 value=load_vector_from(b,V_INPUT,operation(b,SpvOpIAdd,T_UINT,desc[2],operation(b,SpvOpIMul,T_UINT,index,scalar(b,4))),0);
 local_store(b,reg_pointer(b,registers,index),value);emit(b,SpvOpBranch,0,0,1,advance,0,0,0);
 emit(b,SpvOpLabel,0,advance,0,0,0,0,0);local_store(b,iterator,offset(b,index,1));emit(b,SpvOpBranch,0,0,1,h,0,0,0);
 emit(b,SpvOpLabel,0,end,0,0,0,0,0);local_store(b,iterator,scalar(b,0));
 if(control){
  control_dispatch(b,flow,desc[0],swap,available,output,&cf0,&cf1);
  count=field(b,cf0,12,7);address=field(b,cf0,0,4095);sequence=field(b,cf0,16,4095);
  valid=either(b,equal(b,count,scalar(b,0)),fits(b,address,count,available));guard(b,valid,output,2);
  at=operation(b,SpvOpISub,T_UINT,operation(b,SpvOpShiftLeftLogical,T_UINT,scalar(b,1),
   operation(b,SpvOpIMul,T_UINT,count,scalar(b,2))),scalar(b,1));
  valid=both(b,operation(b,SpvOpULessThanEqual,T_BOOL,count,scalar(b,6)),
   equal(b,operation(b,SpvOpBitwiseAnd,T_UINT,operation(b,SpvOpBitwiseAnd,T_UINT,sequence,at),scalar(b,0x555)),scalar(b,0)));
  guard(b,valid,output,3);local_store(b,iterator,scalar(b,0));
 }
 h=id(b);body=id(b);advance=id(b);end=id(b);emit(b,SpvOpBranch,0,0,1,h,0,0,0);
 emit(b,SpvOpLabel,0,h,0,0,0,0,0);index=local_load(b,T_UINT,iterator);
 predicate=operation(b,SpvOpULessThan,T_BOOL,index,count);
 emit(b,SpvOpLoopMerge,0,0,3,end,advance,0,0);emit(b,SpvOpBranchConditional,0,0,3,predicate,body,end,0);
 emit(b,SpvOpLabel,0,body,0,0,0,0,0);
 at=operation(b,SpvOpIAdd,T_UINT,desc[0],operation(b,SpvOpIMul,T_UINT,
  operation(b,SpvOpIAdd,T_UINT,address,index),scalar(b,3)));
 word0=program_word(b,at,swap);word1=program_word(b,offset(b,at,1),swap);word2=program_word(b,offset(b,at,2),swap);
 opcode=field(b,word2,24,31);scalar_opcode=field(b,word0,26,63);
 is_export=yes(b,field(b,word0,15,1));mask_v=field(b,word0,16,15);mask_s=field(b,word0,20,15);
 valid=both(b,either(b,equal(b,opcode,scalar(b,0)),either(b,equal(b,opcode,scalar(b,1)),equal(b,opcode,scalar(b,11)))),
  either(b,equal(b,scalar_opcode,scalar(b,0)),either(b,equal(b,scalar_opcode,scalar(b,44)),equal(b,scalar_opcode,scalar(b,50)))));
 /* Sequence emitter side effects explicitly; C argument evaluation order must
  * not change the established module when adding a separate entry point. */
 a=equal(b,field(b,word1,28,1),scalar(b,0));
 c=equal(b,field(b,word1,30,3),scalar(b,0));d=both(b,a,c);
 a=either(b,is_export,equal(b,field(b,word0,14,1),scalar(b,0)));d=both(b,a,d);
 a=equal(b,field(b,word0,6,1),scalar(b,0));d=both(b,a,d);
 a=equal(b,field(b,word0,24,3),scalar(b,0));d=both(b,a,d);valid=both(b,valid,d);
 valid=both(b,valid,either(b,equal(b,field(b,word0,15,1),scalar(b,0)),equal(b,field(b,word0,0,63),scalar(b,62))));
 if(!control)for(i=0;i<3;++i)valid=both(b,valid,equal(b,
  operation(b,SpvOpBitwiseAnd,T_UINT,field(b,word2,31-i,1),field(b,word2,22-i*8,1)),scalar(b,0)));
 guard(b,valid,output,3);
 a=program_source(b,registers,constants,0,word0,word1,word2,control?flow[11]:0,output);
 c=program_source(b,registers,constants,1,word0,word1,word2,control?flow[11]:0,output);
 d=program_source(b,registers,constants,2,word0,word1,word2,control?flow[11]:0,output);
 value=vector_alu(b,opcode,a,c,d);
 scalar_value=scalar_alu(b,scalar_opcode,opcode,d,previous,registers,constants,word0,word1,word2);
 store_export=id(b);store_temp=id(b);stored=id(b);
 emit(b,SpvOpSelectionMerge,0,0,2,stored,0,0,0);emit(b,SpvOpBranchConditional,0,0,3,is_export,store_export,store_temp,0);
 emit(b,SpvOpLabel,0,store_export,0,0,0,0,0);
 overlap=operation(b,SpvOpBitwiseAnd,T_UINT,mask_v,mask_s);
 union_mask=operation(b,SpvOpBitwiseOr,T_UINT,mask_v,mask_s);
 zero_mask=select_word(b,yes(b,field(b,word0,14,1)),scalar(b,15),scalar(b,0));
 at=local_load(b,T_U4,position);
 at=choose(b,mask_predicate(b,zero_mask),vector(b,0),at);
 at=choose(b,mask_predicate(b,mask_s),scalar_value,at);
 at=choose(b,mask_predicate(b,mask_v),value,at);
 at=choose(b,mask_predicate(b,overlap),vector(b,0x3f800000),at);
 local_store(b,position,at);local_store(b,exports,offset(b,local_load(b,T_UINT,exports),1));
 local_store(b,written,operation(b,SpvOpBitwiseOr,T_UINT,local_load(b,T_UINT,written),
  operation(b,SpvOpBitwiseOr,T_UINT,union_mask,zero_mask)));
 emit(b,SpvOpBranch,0,0,1,stored,0,0,0);
 emit(b,SpvOpLabel,0,store_temp,0,0,0,0,0);
 register_write(b,registers,field(b,word0,0,63),mask_v,value);
 register_write(b,registers,field(b,word0,8,63),mask_s,scalar_value);
 emit(b,SpvOpBranch,0,0,1,stored,0,0,0);
 emit(b,SpvOpLabel,0,stored,0,0,0,0,0);emit(b,SpvOpBranch,0,0,1,advance,0,0,0);
 emit(b,SpvOpLabel,0,advance,0,0,0,0,0);local_store(b,iterator,offset(b,index,1));emit(b,SpvOpBranch,0,0,1,h,0,0,0);
 emit(b,SpvOpLabel,0,end,0,0,0,0,0);
 if(control){
  predicate=either(b,equal(b,flow[7],scalar(b,2)),equal(b,flow[7],scalar(b,4)));
  emit(b,SpvOpBranchConditional,0,0,3,predicate,flow[6],flow[8],0);
  emit(b,SpvOpLabel,0,flow[8],0,0,0,0,0);emit(b,SpvOpBranch,0,0,1,flow[5],0,0,0);
  emit(b,SpvOpLabel,0,flow[5],0,0,0,0,0);emit(b,SpvOpBranch,0,0,1,flow[4],0,0,0);
  emit(b,SpvOpLabel,0,flow[6],0,0,0,0,0);
 }
 guard(b,equal(b,local_load(b,T_UINT,written),scalar(b,15)),output,4);
 value=local_load(b,T_U4,position);
 for(i=0;i<4;++i){at=id(b);emit(b,SpvOpCompositeExtract,T_UINT,at,2,value,i,0,0);store_word(b,offset(b,output,i),at);}
 store_word(b,offset(b,output,4),scalar(b,1));store_word(b,offset(b,output,5),local_load(b,T_UINT,exports));
 store_word(b,offset(b,output,6),local_load(b,T_UINT,written));
 emit(b,SpvOpReturn,0,0,0,0,0,0,0);emit(b,SpvOpFunctionEnd,0,0,0,0,0,0,0);
}

static int build(xe_spv_binary *out,int program) {
 alu_builder b;unsigned i;uint32_t v[5];
 static const uint32_t values[]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,
  19,20,21,22,23,24,25,26,27,28,29,30,31,32,44,50,62,63,64,127,255,256,382,512,4095,65536,UINT32_C(0x3f800000),
 UINT32_C(0x0555),
  UINT32_C(0x1000000),UINT32_C(0x10000000),UINT32_C(0x40000000),
  UINT32_C(0x7fffffff),UINT32_C(0x80000000),UINT32_C(0x7fffff),
  UINT32_C(0x800000),UINT32_C(0x7f800000),UINT32_C(0x7f7fffff),UINT32_C(0x7fc00000),UINT32_C(0xffffffff)};
 if(!out)return 0;
 xe_spv_binary_reset(out);if(!xe_spv_binary_reserve(out,program?8192:4096))return 0;
 memset(&b,0,sizeof(b));b.out=out;b.next=64;
 v[0]=UINT32_C(0x07230203);v[1]=UINT32_C(0x00010000);v[2]=0;v[3]=0;v[4]=0;
 xe_spv_binary_append(out,v,5);
 emit(&b,SpvOpCapability,0,0,1,SpvCapabilityShader,0,0,0);
 emit(&b,SpvOpMemoryModel,0,0,2,SpvAddressingModelLogical,SpvMemoryModelGLSL450,0,0);
 v[0]=SpvExecutionModelGLCompute;v[1]=F_MAIN;v[2]=UINT32_C(0x6e69616d);v[3]=0;v[4]=V_INVOCATION;
 xe_spv_binary_instruction(out,SpvOpEntryPoint,0,0,v,5);
 v[0]=F_MAIN;v[1]=SpvExecutionModeLocalSize;v[2]=64;v[3]=1;v[4]=1;
 xe_spv_binary_instruction(out,SpvOpExecutionMode,0,0,v,5);
 emit(&b,SpvOpDecorate,0,0,3,V_INVOCATION,SpvDecorationBuiltIn,SpvBuiltInGlobalInvocationId,0);
 emit(&b,SpvOpDecorate,0,0,3,T_ARRAY,SpvDecorationArrayStride,4,0);
 emit(&b,SpvOpDecorate,0,0,2,T_BUFFER,SpvDecorationBufferBlock,0,0);
 emit(&b,SpvOpMemberDecorate,0,0,4,T_BUFFER,0,SpvDecorationOffset,0);
 if(program==2){
  emit(&b,SpvOpDecorate,0,0,3,T_CONTEXT_ARRAY,SpvDecorationArrayStride,16,0);
  emit(&b,SpvOpDecorate,0,0,2,T_CONTEXT,SpvDecorationBlock,0,0);
  emit(&b,SpvOpMemberDecorate,0,0,4,T_CONTEXT,0,SpvDecorationOffset,0);
  emit(&b,SpvOpDecorate,0,0,3,V_CONTEXT,SpvDecorationDescriptorSet,0,0);
  emit(&b,SpvOpDecorate,0,0,3,V_CONTEXT,SpvDecorationBinding,4,0);
 }
 emit(&b,SpvOpDecorate,0,0,3,V_INPUT,SpvDecorationDescriptorSet,0,0);
 emit(&b,SpvOpDecorate,0,0,3,V_INPUT,SpvDecorationBinding,0,0);
 emit(&b,SpvOpDecorate,0,0,2,V_INPUT,SpvDecorationNonWritable,0,0);
 emit(&b,SpvOpDecorate,0,0,3,V_OUTPUT,SpvDecorationDescriptorSet,0,0);
 emit(&b,SpvOpDecorate,0,0,3,V_OUTPUT,SpvDecorationBinding,program?3:1,0);
 if(program){
  emit(&b,SpvOpDecorate,0,0,3,V_PROGRAM,SpvDecorationDescriptorSet,0,0);
  emit(&b,SpvOpDecorate,0,0,3,V_PROGRAM,SpvDecorationBinding,1,0);
  emit(&b,SpvOpDecorate,0,0,2,V_PROGRAM,SpvDecorationNonWritable,0,0);
  emit(&b,SpvOpDecorate,0,0,3,V_CONSTANT,SpvDecorationDescriptorSet,0,0);
  emit(&b,SpvOpDecorate,0,0,3,V_CONSTANT,SpvDecorationBinding,2,0);
  emit(&b,SpvOpDecorate,0,0,2,V_CONSTANT,SpvDecorationNonWritable,0,0);
 }
 emit(&b,SpvOpTypeVoid,0,T_VOID,0,0,0,0,0);
 emit(&b,SpvOpTypeInt,0,T_UINT,2,32,0,0,0);
 emit(&b,SpvOpTypeVector,0,T_U3,2,T_UINT,3,0,0);emit(&b,SpvOpTypeVector,0,T_U4,2,T_UINT,4,0,0);
 emit(&b,SpvOpTypeBool,0,T_BOOL,0,0,0,0,0);emit(&b,SpvOpTypeVector,0,T_B4,2,T_BOOL,4,0,0);
 emit(&b,SpvOpTypeStruct,0,T_PAIR,2,T_U4,T_U4,0,0);
 emit(&b,SpvOpTypeRuntimeArray,0,T_ARRAY,1,T_UINT,0,0,0);
 emit(&b,SpvOpTypeStruct,0,T_BUFFER,1,T_ARRAY,0,0,0);
 emit(&b,SpvOpTypePointer,0,T_PBUFFER,2,SpvStorageClassUniform,T_BUFFER,0,0);
 emit(&b,SpvOpTypePointer,0,T_PUINT,2,SpvStorageClassUniform,T_UINT,0,0);
 emit(&b,SpvOpTypePointer,0,T_PINPUT,2,SpvStorageClassInput,T_U3,0,0);
 emit(&b,SpvOpTypeFunction,0,T_MAIN_FN,1,T_VOID,0,0,0);
 emit(&b,SpvOpTypeFunction,0,T_MATH_FN,3,T_U4,T_U4,T_U4,0);
 for(i=0;i<sizeof(values)/sizeof(values[0]);++i){
  alu_constant *c=&b.constant[b.constants++];c->literal=values[i];c->scalar=id(&b);c->vector=id(&b);
  emit(&b,SpvOpConstant,T_UINT,c->scalar,1,c->literal,0,0,0);
  emit(&b,SpvOpConstantComposite,T_U4,c->vector,4,c->scalar,c->scalar,c->scalar,c->scalar);
 }
 if(program==2){
  static const uint32_t extra[3]={4096,8191,65535};
  for(i=0;i<3;++i){
   alu_constant *c=&b.constant[b.constants++];c->literal=extra[i];c->scalar=id(&b);c->vector=id(&b);
   emit(&b,SpvOpConstant,T_UINT,c->scalar,1,c->literal,0,0,0);
   emit(&b,SpvOpConstantComposite,T_U4,c->vector,4,c->scalar,c->scalar,c->scalar,c->scalar);
  }
 }
 if(program){
  emit(&b,SpvOpTypeArray,0,T_REG_ARRAY,2,T_U4,scalar(&b,64),0,0);
  emit(&b,SpvOpTypePointer,0,T_PREG_ARRAY,2,SpvStorageClassFunction,T_REG_ARRAY,0,0);
  emit(&b,SpvOpTypePointer,0,T_PREG,2,SpvStorageClassFunction,T_U4,0,0);
  emit(&b,SpvOpTypePointer,0,T_PLOCAL_UINT,2,SpvStorageClassFunction,T_UINT,0,0);
  emit(&b,SpvOpConstantNull,T_REG_ARRAY,C_NULL_REG,0,0,0,0,0);
  emit(&b,SpvOpVariable,T_PBUFFER,V_PROGRAM,1,SpvStorageClassUniform,0,0,0);
  emit(&b,SpvOpVariable,T_PBUFFER,V_CONSTANT,1,SpvStorageClassUniform,0,0,0);
 }
 if(program==2){
  emit(&b,SpvOpTypeArray,0,T_CONTEXT_ARRAY,2,T_U4,scalar(&b,10),0,0);
  emit(&b,SpvOpTypeStruct,0,T_CONTEXT,1,T_CONTEXT_ARRAY,0,0,0);
  emit(&b,SpvOpTypePointer,0,T_PCONTEXT,2,SpvStorageClassUniform,T_CONTEXT,0,0);
  emit(&b,SpvOpVariable,T_PCONTEXT,V_CONTEXT,1,SpvStorageClassUniform,0,0,0);
 }
 emit(&b,SpvOpVariable,T_PBUFFER,V_INPUT,1,SpvStorageClassUniform,0,0,0);
 emit(&b,SpvOpVariable,T_PBUFFER,V_OUTPUT,1,SpvStorageClassUniform,0,0,0);
 emit(&b,SpvOpVariable,T_PINPUT,V_INVOCATION,1,SpvStorageClassInput,0,0,0);
 math_function(&b,0);math_function(&b,1);if(program)program_entry(&b,program==2);else entry(&b);
 if(out->failed){out->count=0;return 0;}out->words[3]=b.next;return 1;
}

int xe_alu_spv_build(xe_spv_binary *out){return build(out,0);}
int xe_alu_program_spv_build(xe_spv_binary *out){return build(out,1);}
int xe_alu_control_spv_build(xe_spv_binary *out){return build(out,2);}
