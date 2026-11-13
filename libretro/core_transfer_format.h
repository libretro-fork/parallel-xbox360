/* Portable raw-IR EDRAM format construction. BSD; MSVC C89.
 * Rules follow Xenia's native EDRAM emitters (Ben Vanik and contributors).
 * Borrowed SSA IDs only; no pixel payload, allocation or intermediate graph.
 * This constructs shader code at initialization, not during a transfer. */
#ifndef XE_CORE_TRANSFER_FORMAT_H
#define XE_CORE_TRANSFER_FORMAT_H
#include <stdint.h>
#include <string.h>
#include "core_transfer_sample.h"

enum xe_transfer_format_op {
 XE_TF_U32,XE_TF_F32_BITS,XE_TF_ADD_U32,XE_TF_SUB_U32,XE_TF_AND_U32,XE_TF_OR_U32,
 XE_TF_SHL_U32,XE_TF_SHR_U32,XE_TF_EQ_U32,XE_TF_NE_U32,XE_TF_LT_U32,XE_TF_GE_U32,
 XE_TF_SELECT_U32,XE_TF_SELECT_F32,XE_TF_ADD_F32,XE_TF_MUL_F32,
 XE_TF_LT_F32,XE_TF_GT_F32,XE_TF_GE_F32,XE_TF_U32_TO_F32,XE_TF_F32_TO_U32,
 XE_TF_BITCAST_U32_F32,XE_TF_BITCAST_F32_U32,XE_TF_TRUNC_F32,XE_TF_SATURATE_F32,
 /* Ordered [0,b] clamp, with NaN and either zero mapped to positive zero.
  * b is a positive finite float ID supplied by the format recipe. */
 XE_TF_CLAMP_POSITIVE_F32,XE_TF_ROUND_EVEN_F32
};
typedef uintptr_t (*xe_transfer_format_emit_fn)(void *,enum xe_transfer_format_op,uintptr_t,uintptr_t,uintptr_t);
typedef struct xe_transfer_format_emitter {
 xe_transfer_format_emit_fn emit;void *context;unsigned failed;
} xe_transfer_format_emitter;
typedef char xe_transfer_format_float_size[sizeof(float)==sizeof(uint32_t)?1:-1];

static INLINE uintptr_t xe_tf_op(xe_transfer_format_emitter *e,enum xe_transfer_format_op op,
 uintptr_t a,uintptr_t b,uintptr_t c) {
 uintptr_t value;
 if(!e || !e->emit || e->failed)return 0;
 value=e->emit(e->context,op,a,b,c);if(!value)e->failed=1;return value;
}
static INLINE uintptr_t xe_tf_u(xe_transfer_format_emitter *e,uint32_t value) {
 return xe_tf_op(e,XE_TF_U32,value,0,0);
}
static INLINE uintptr_t xe_tf_f(xe_transfer_format_emitter *e,float value) {
 uint32_t bits;memcpy(&bits,&value,sizeof(bits));return xe_tf_op(e,XE_TF_F32_BITS,bits,0,0);
}
static INLINE uintptr_t xe_tf_binary(xe_transfer_format_emitter *e,enum xe_transfer_format_op op,uintptr_t a,uintptr_t b) {
 if(!a || !b){if(e)e->failed=1;return 0;}return xe_tf_op(e,op,a,b,0);
}
static INLINE uintptr_t xe_tf_unary(xe_transfer_format_emitter *e,enum xe_transfer_format_op op,uintptr_t a) {
 if(!a){if(e)e->failed=1;return 0;}return xe_tf_op(e,op,a,0,0);
}
static INLINE uintptr_t xe_tf_select(xe_transfer_format_emitter *e,unsigned floating,uintptr_t condition,uintptr_t a,uintptr_t b) {
 if(!condition || !a || !b){if(e)e->failed=1;return 0;}
 return xe_tf_op(e,floating?XE_TF_SELECT_F32:XE_TF_SELECT_U32,condition,a,b);
}
static INLINE uintptr_t xe_tf_field(xe_transfer_format_emitter *e,uintptr_t value,unsigned shift,uint32_t mask) {
 if(shift)value=xe_tf_binary(e,XE_TF_SHR_U32,value,xe_tf_u(e,shift));
 return xe_tf_binary(e,XE_TF_AND_U32,value,xe_tf_u(e,mask));
}
static INLINE int xe_tf_ready(const xe_transfer_format_emitter *e,uintptr_t value) {
 return e && e->emit && !e->failed && value;
}
static INLINE uintptr_t xe_tf_pack_small(xe_transfer_format_emitter *e,uintptr_t value,unsigned bias,
 unsigned shift,uint32_t mask,unsigned round_even) {
 uintptr_t bits,amount,denormal,normal,biased;
 bits=xe_tf_unary(e,XE_TF_BITCAST_F32_U32,value);
 amount=xe_tf_binary(e,XE_TF_SUB_U32,xe_tf_u(e,bias+1),xe_tf_binary(e,XE_TF_SHR_U32,bits,xe_tf_u(e,23)));
 amount=xe_tf_select(e,0,xe_tf_binary(e,XE_TF_LT_U32,amount,xe_tf_u(e,24)),amount,xe_tf_u(e,24));
 denormal=xe_tf_binary(e,XE_TF_SHR_U32,xe_tf_binary(e,XE_TF_OR_U32,xe_tf_field(e,bits,0,0x7fffff),xe_tf_u(e,0x800000)),amount);
 normal=xe_tf_binary(e,XE_TF_SUB_U32,bits,xe_tf_u(e,bias<<23));
 biased=xe_tf_select(e,0,xe_tf_binary(e,XE_TF_LT_U32,bits,xe_tf_u(e,(bias+1)<<23)),denormal,normal);
 if(round_even)biased=xe_tf_binary(e,XE_TF_ADD_U32,
  xe_tf_binary(e,XE_TF_ADD_U32,biased,xe_tf_u(e,(1u<<(shift-1))-1)),xe_tf_field(e,biased,shift,1));
 return xe_tf_field(e,biased,shift,mask);
}
static INLINE uintptr_t xe_tf_unpack_small(xe_transfer_format_emitter *e,uintptr_t value,unsigned mantissa,
 uint32_t exponent_mask,unsigned bias,float denormal_scale) {
 uintptr_t exponent,fraction,normal;
 exponent=xe_tf_field(e,value,mantissa,exponent_mask);fraction=xe_tf_field(e,value,0,(1u<<mantissa)-1);
 normal=xe_tf_binary(e,XE_TF_OR_U32,
  xe_tf_binary(e,XE_TF_SHL_U32,xe_tf_binary(e,XE_TF_ADD_U32,exponent,xe_tf_u(e,bias)),xe_tf_u(e,23)),
  xe_tf_binary(e,XE_TF_SHL_U32,fraction,xe_tf_u(e,23-mantissa)));
 return xe_tf_select(e,1,xe_tf_binary(e,XE_TF_NE_U32,exponent,xe_tf_u(e,0)),xe_tf_unary(e,XE_TF_BITCAST_U32_F32,normal),
  xe_tf_binary(e,XE_TF_MUL_F32,xe_tf_unary(e,XE_TF_U32_TO_F32,fraction),xe_tf_f(e,denormal_scale)));
}
static INLINE uintptr_t xe_transfer_format_pack7e3(xe_transfer_format_emitter *e,uintptr_t value) {
 uintptr_t bits,denormal,normal,rounded;
 if(!xe_tf_ready(e,value))return 0;
 value=xe_tf_binary(e,XE_TF_CLAMP_POSITIVE_F32,value,xe_tf_f(e,31.875f));
 bits=xe_tf_unary(e,XE_TF_BITCAST_F32_U32,value);
 /* Scaling by 2^9 is exact; RoundEven explicitly quantizes the subnormal
  * interval. Do not depend on the device's default F32 addition mode. */
 denormal=xe_tf_unary(e,XE_TF_F32_TO_U32,xe_tf_unary(e,XE_TF_ROUND_EVEN_F32,
  xe_tf_binary(e,XE_TF_MUL_F32,value,xe_tf_f(e,512.0f))));
 normal=xe_tf_binary(e,XE_TF_SUB_U32,bits,xe_tf_u(e,124u<<23));
 rounded=xe_tf_binary(e,XE_TF_ADD_U32,
  xe_tf_binary(e,XE_TF_ADD_U32,normal,xe_tf_u(e,0x7fff)),xe_tf_field(e,normal,16,1));
 normal=xe_tf_field(e,rounded,16,1023);
 return xe_tf_select(e,0,xe_tf_binary(e,XE_TF_LT_U32,bits,xe_tf_u(e,0x3e800000)),denormal,normal);
}
static INLINE uintptr_t xe_transfer_format_unpack7e3(xe_transfer_format_emitter *e,uintptr_t value) {
 return xe_tf_ready(e,value)?xe_tf_unpack_small(e,value,7,7,124,.001953125f):0;
}
/* Caller supplies pre-clamped depth. remap maps host[0,1) to guest[0,2). */
static INLINE uintptr_t xe_transfer_format_pack20e4(xe_transfer_format_emitter *e,uintptr_t value,unsigned round_even,unsigned remap) {
 return xe_tf_ready(e,value) && round_even<2 && remap<2?xe_tf_pack_small(e,value,remap?111:112,3,0xffffff,round_even):0;
}
static INLINE uintptr_t xe_transfer_format_unpack20e4(xe_transfer_format_emitter *e,uintptr_t value,unsigned remap) {
 return xe_tf_ready(e,value) && remap<2?xe_tf_unpack_small(e,value,20,15,remap?111:112,
  remap?2.910383045673370361328125e-11f:5.82076609134674072265625e-11f):0;
}
/* Gamma inputs are pre-saturated linear/gamma values; callers leave alpha
 * outside these RGB operations, as in the existing transfer implementation. */
static INLINE uintptr_t xe_transfer_format_gamma_encode(xe_transfer_format_emitter *e,uintptr_t value) {
 static const float thresholds[3]={64.0f/1023.0f,128.0f/1023.0f,512.0f/1023.0f};
 static const float scales[3]={511.5f,255.75f,127.875f},offsets[3]={32.0f/255.0f,64.0f/255.0f,128.0f/255.0f};
 uintptr_t scale,offset,condition;unsigned i;
 if(!xe_tf_ready(e,value))return 0;
 scale=xe_tf_f(e,1023);offset=xe_tf_f(e,0);
 for(i=0;i<3;++i){condition=xe_tf_binary(e,XE_TF_GE_F32,value,xe_tf_f(e,thresholds[i]));
  scale=xe_tf_select(e,1,condition,xe_tf_f(e,scales[i]),scale);offset=xe_tf_select(e,1,condition,xe_tf_f(e,offsets[i]),offset);}
 value=xe_tf_unary(e,XE_TF_TRUNC_F32,xe_tf_binary(e,XE_TF_MUL_F32,value,scale));
 return xe_tf_binary(e,XE_TF_ADD_F32,xe_tf_binary(e,XE_TF_MUL_F32,value,xe_tf_f(e,1.0f/255.0f)),offset);
}
static INLINE uintptr_t xe_transfer_format_gamma_decode(xe_transfer_format_emitter *e,uintptr_t value) {
 static const float thresholds[3]={64.0f/255.0f,96.0f/255.0f,192.0f/255.0f};
 static const float scales[3]={2.0f/1024.0f,4.0f/1024.0f,8.0f/1024.0f},offsets[3]={-64,-256,-1024};
 uintptr_t scale,offset,condition,linear,truncated;unsigned i;
 if(!xe_tf_ready(e,value))return 0;
 scale=xe_tf_f(e,1.0f/1024.0f);offset=xe_tf_f(e,0);
 for(i=0;i<3;++i){condition=xe_tf_binary(e,XE_TF_GE_F32,value,xe_tf_f(e,thresholds[i]));
  scale=xe_tf_select(e,1,condition,xe_tf_f(e,scales[i]),scale);offset=xe_tf_select(e,1,condition,xe_tf_f(e,offsets[i]),offset);}
 linear=xe_tf_binary(e,XE_TF_ADD_F32,xe_tf_binary(e,XE_TF_MUL_F32,
  xe_tf_binary(e,XE_TF_MUL_F32,value,xe_tf_f(e,255.0f*1024.0f)),scale),offset);
 truncated=xe_tf_unary(e,XE_TF_TRUNC_F32,xe_tf_binary(e,XE_TF_MUL_F32,linear,scale));
 return xe_tf_binary(e,XE_TF_MUL_F32,xe_tf_binary(e,XE_TF_ADD_F32,linear,truncated),xe_tf_f(e,1.0f/1023.0f));
}
static INLINE uintptr_t xe_tf_gamma_byte(xe_transfer_format_emitter *e,uintptr_t value) {
 static const unsigned thresholds[3]={64,96,192};
 static const float scales[3]={1.0f/511.5f,1.0f/255.75f,1.0f/127.875f};
 static const float offsets[3]={-31.5f/511.5f,-63.5f/255.75f,-127.5f/127.875f};
 uintptr_t scale=xe_tf_f(e,1.0f/1023.0f),offset=xe_tf_f(e,.5f/1023.0f),condition;unsigned i;
 for(i=0;i<3;++i){condition=xe_tf_binary(e,XE_TF_GE_U32,value,xe_tf_u(e,thresholds[i]));
  scale=xe_tf_select(e,1,condition,xe_tf_f(e,scales[i]),scale);offset=xe_tf_select(e,1,condition,xe_tf_f(e,offsets[i]),offset);}
 return xe_tf_binary(e,XE_TF_ADD_F32,xe_tf_binary(e,XE_TF_MUL_F32,xe_tf_unary(e,XE_TF_U32_TO_F32,value),scale),offset);
}
static INLINE unsigned xe_transfer_format_words(unsigned format) {
 switch(format){case 0:case 1:case 2:case 3:case 4:case 6:case 10:case 12:case 14:return 1;
  case 5:case 7:case 15:return 2;default:return 0;}
}
static INLINE unsigned xe_transfer_format_components(unsigned format) {
 return format==14?1:format==4 || format==6 || format==15?2:4;
}
/* Direct numeric transfers must precede raw reinterpretation. Identical views
 * borrow the fetched IDs unchanged; plain/gamma byte layouts convert RGB only.
 * Going through a packed byte here would quantize identity values and would
 * substitute gamma-byte midpoints for the direct conversion's lower edges.
 * Formats describe actual backend storage, after its gamma/alias policy. */
static INLINE int xe_transfer_format_direct_color(xe_transfer_format_emitter *e,
 unsigned source_format,unsigned dest_format,unsigned integer_view,
 const uintptr_t source[4],uintptr_t out[4]) {
 uintptr_t result[4];unsigned i;
 if(!e || !e->emit || e->failed || !source || !out || integer_view>1)return 0;
 if(source_format==10)source_format=2;
 if(source_format==12)source_format=3;
 if(dest_format==10)dest_format=2;
 if(dest_format==12)dest_format=3;
 if(integer_view){
  if(source_format==6)source_format=4;
  if(source_format==7)source_format=5;
  if(dest_format==6)dest_format=4;
  if(dest_format==7)dest_format=5;
 }
 if(!xe_transfer_format_words(source_format) || !xe_transfer_format_words(dest_format) ||
    (integer_view && (source_format<4 || dest_format<4)) ||
    (!integer_view && ((source_format>=4 && source_format<=7) || (dest_format>=4 && dest_format<=7))))return 0;
 if(source_format!=dest_format && !(source_format<2 && dest_format<2 && !integer_view))return 0;
 for(i=0;i<4;++i){if(!source[i])return 0;result[i]=source[i];}
 if(source_format!=dest_format)for(i=0;i<3;++i)result[i]=source_format==1?
  xe_transfer_format_gamma_encode(e,source[i]):xe_transfer_format_gamma_decode(e,source[i]);
 if(e->failed)return 0;
 for(i=0;i<4;++i)out[i]=result[i];
 return 1;
}
/* Formats are compile-time branch recipes; a generic shader selects the ready
 * recipe through uniform control flow. Raw integer views preserve every bit.
 * Failure leaves the caller's result IDs intact; discard failed shader state. */
static INLINE unsigned xe_transfer_format_pack_color(xe_transfer_format_emitter *e,unsigned format,unsigned source_uint,
 const uintptr_t source[4],uintptr_t out[2]) {
 unsigned i,width,shift,word,count=xe_transfer_format_words(format),components=xe_transfer_format_components(format);
 uintptr_t value,packed[2];
 if(!e || !e->emit || e->failed || !source || !out || !count || source_uint>1 ||
  (format<4 && source_uint) || ((format>=4 && format<=7) && !source_uint) ||
  ((format==10 || format==12) && source_uint))return 0;
 for(i=0;i<components;++i)if(!source[i])return 0;
 packed[0]=xe_tf_u(e,0);packed[1]=packed[0];
 for(i=0;i<components;++i){
  value=source[i];
  if(format>=14){packed[i]=source_uint?value:xe_tf_unary(e,XE_TF_BITCAST_F32_U32,value);continue;}
  if(format>=4 && format<=7){width=16;word=i/2;shift=(i&1)*16;}
  else{word=0;width=format<2?8:i<3?10:2;shift=format<2?i*8:i*10;
   if(i<3 && (format==3 || format==12))value=xe_transfer_format_pack7e3(e,value);
   else{
    if(format==1 && i<3)value=xe_transfer_format_gamma_encode(e,value);
    if(i==3 && (format==3 || format==12))value=xe_tf_unary(e,XE_TF_SATURATE_F32,value);
    value=xe_tf_unary(e,XE_TF_F32_TO_U32,xe_tf_binary(e,XE_TF_ADD_F32,
     xe_tf_binary(e,XE_TF_MUL_F32,value,xe_tf_f(e,(float)((1u<<width)-1))),xe_tf_f(e,.5f)));
   }
  }
  value=xe_tf_field(e,value,0,(1u<<width)-1);
  packed[word]=xe_tf_binary(e,XE_TF_OR_U32,packed[word],shift?xe_tf_binary(e,XE_TF_SHL_U32,value,xe_tf_u(e,shift)):value);
 }
 if(e->failed)return 0;
 out[0]=packed[0];out[1]=packed[1];return count;
}
static INLINE unsigned xe_transfer_format_unpack_color(xe_transfer_format_emitter *e,unsigned format,unsigned dest_uint,
 const uintptr_t packed[2],uintptr_t out[4]) {
 unsigned i,width,shift,count=xe_transfer_format_words(format),components=xe_transfer_format_components(format);
 uintptr_t value,dest[4];
 if(!e || !e->emit || e->failed || !packed || !out || !count || dest_uint>1 ||
  !packed[0] || (count==2 && !packed[1]) || (format<4 && dest_uint) ||
  ((format>=4 && format<=7) && !dest_uint) || ((format==10 || format==12) && dest_uint))return 0;
 for(i=0;i<4;++i){
  dest[i]=dest_uint?xe_tf_u(e,0):xe_tf_f(e,0);if(i>=components)continue;
  if(format>=14){dest[i]=dest_uint?packed[i]:xe_tf_unary(e,XE_TF_BITCAST_U32_F32,packed[i]);continue;}
  if(format>=4 && format<=7){dest[i]=xe_tf_field(e,packed[i/2],(i&1)*16,65535);continue;}
  width=format<2?8:i<3?10:2;shift=format<2?i*8:i*10;
  value=xe_tf_field(e,packed[0],shift,(1u<<width)-1);
  if(i<3 && (format==3 || format==12))value=xe_transfer_format_unpack7e3(e,value);
  else if(format==1 && i<3)value=xe_tf_gamma_byte(e,value);
  else value=xe_tf_binary(e,XE_TF_MUL_F32,xe_tf_unary(e,XE_TF_U32_TO_F32,value),xe_tf_f(e,1.0f/(float)((1u<<width)-1)));
  dest[i]=value;
 }
 if(e->failed)return 0;
 for(i=0;i<4;++i)out[i]=dest[i];
 return count;
}
#endif
