/* Integer ownership views: bit-preserving generic color codec. BSD; C89.
 * Format families4/6,5/7,14,15 retain all NaN and signed-minimum encodings.
 * This is not a numeric color conversion or a complete transfer shader. */
#ifndef XE_CORE_TRANSFER_UINT_COLOR_H
#define XE_CORE_TRANSFER_UINT_COLOR_H
#include "core_transfer_sample.h"
typedef struct xe_transfer_uint_color_config {
 uint32_t halves,wide;
} xe_transfer_uint_color_config;
typedef struct xe_transfer_uint_color_parameters {
 /* halves: all bits set for16-bit components, zero for32-bit components.
  * wide: all bits set for64-bit samples, zero for32-bit samples.
  * shift16 is the borrowed ID of the literal16, not a dynamic shift. */
 uintptr_t halves,wide,shift16;
} xe_transfer_uint_color_parameters;
static INLINE int xe_transfer_uint_color_prepare(xe_transfer_uint_color_config *out,
 unsigned format) {
 xe_transfer_uint_color_config value;
 if(!out)return 0;
 switch(format) {
  case 4:case 6:value.halves=UINT32_MAX;value.wide=0;break;
  case 5:case 7:value.halves=UINT32_MAX;value.wide=UINT32_MAX;break;
  case 14:value.halves=0;value.wide=0;break;
  case 15:value.halves=0;value.wide=UINT32_MAX;break;
  default:return 0;
 }
 *out=value;return 1;
}
/* Same borrowed native-u32 IR convention as sample addressing. All source
 * components must have defined IDs; inactive payload lanes are masked out.
 * No width-dependent shift, float operation, heap storage or copied payload. */
static INLINE int xe_transfer_uint_color_pack(xe_transfer_sample_emitter *e,
 const xe_transfer_uint_color_parameters *p,const uintptr_t source[4],uintptr_t out[2]) {
 uintptr_t all,low16,not_half,mask,a,b,c,result[2];
 if(!e || !e->emit || !p || !source || !out || !p->halves || !p->wide ||
    !p->shift16 || !source[0] || !source[1] || !source[2] || !source[3])return 0;
#define XE_COLOR_OP(dst,op,a,b,c) do { \
 dst=e->emit(e->context,op,a,b,c);if(!dst)return 0; \
} while(0)
 XE_COLOR_OP(all,XE_TRANSFER_SAMPLE_CONST,UINT32_MAX,0,0);
 XE_COLOR_OP(low16,XE_TRANSFER_SAMPLE_SHR,all,p->shift16,0);
 XE_COLOR_OP(not_half,XE_TRANSFER_SAMPLE_XOR,p->halves,all,0);
 XE_COLOR_OP(mask,XE_TRANSFER_SAMPLE_OR,not_half,low16,0);
 XE_COLOR_OP(a,XE_TRANSFER_SAMPLE_AND,source[0],mask,0);
 XE_COLOR_OP(b,XE_TRANSFER_SAMPLE_SHL,source[1],p->shift16,0);
 XE_COLOR_OP(b,XE_TRANSFER_SAMPLE_AND,b,p->halves,0);
 XE_COLOR_OP(result[0],XE_TRANSFER_SAMPLE_OR,a,b,0);
 XE_COLOR_OP(a,XE_TRANSFER_SAMPLE_AND,source[2],mask,0);
 XE_COLOR_OP(b,XE_TRANSFER_SAMPLE_SHL,source[3],p->shift16,0);
 XE_COLOR_OP(c,XE_TRANSFER_SAMPLE_OR,a,b,0);
 XE_COLOR_OP(c,XE_TRANSFER_SAMPLE_AND,c,p->halves,0);
 XE_COLOR_OP(a,XE_TRANSFER_SAMPLE_AND,source[1],not_half,0);
 XE_COLOR_OP(c,XE_TRANSFER_SAMPLE_OR,c,a,0);
 XE_COLOR_OP(result[1],XE_TRANSFER_SAMPLE_AND,c,p->wide,0);
 out[0]=result[0];out[1]=result[1];
#undef XE_COLOR_OP
 return 1;
}
static INLINE int xe_transfer_uint_color_unpack(xe_transfer_sample_emitter *e,
 const xe_transfer_uint_color_parameters *p,const uintptr_t packed[2],uintptr_t out[4]) {
 uintptr_t all,low16,not_half,mask,a,b,c,extra,result[4];
 if(!e || !e->emit || !p || !packed || !out || !p->halves || !p->wide ||
    !p->shift16 || !packed[0] || !packed[1])return 0;
#define XE_COLOR_OP(dst,op,a,b,c) do { \
 dst=e->emit(e->context,op,a,b,c);if(!dst)return 0; \
} while(0)
 XE_COLOR_OP(all,XE_TRANSFER_SAMPLE_CONST,UINT32_MAX,0,0);
 XE_COLOR_OP(low16,XE_TRANSFER_SAMPLE_SHR,all,p->shift16,0);
 XE_COLOR_OP(not_half,XE_TRANSFER_SAMPLE_XOR,p->halves,all,0);
 XE_COLOR_OP(mask,XE_TRANSFER_SAMPLE_OR,not_half,low16,0);
 XE_COLOR_OP(result[0],XE_TRANSFER_SAMPLE_AND,packed[0],mask,0);
 XE_COLOR_OP(a,XE_TRANSFER_SAMPLE_SHR,packed[0],p->shift16,0);
 XE_COLOR_OP(a,XE_TRANSFER_SAMPLE_AND,a,p->halves,0);
 XE_COLOR_OP(b,XE_TRANSFER_SAMPLE_AND,packed[1],not_half,0);
 XE_COLOR_OP(c,XE_TRANSFER_SAMPLE_OR,a,b,0);
 XE_COLOR_OP(a,XE_TRANSFER_SAMPLE_OR,p->halves,p->wide,0);
 XE_COLOR_OP(result[1],XE_TRANSFER_SAMPLE_AND,c,a,0);
 XE_COLOR_OP(extra,XE_TRANSFER_SAMPLE_AND,p->halves,p->wide,0);
 XE_COLOR_OP(a,XE_TRANSFER_SAMPLE_AND,packed[1],low16,0);
 XE_COLOR_OP(result[2],XE_TRANSFER_SAMPLE_AND,a,extra,0);
 XE_COLOR_OP(b,XE_TRANSFER_SAMPLE_SHR,packed[1],p->shift16,0);
 XE_COLOR_OP(result[3],XE_TRANSFER_SAMPLE_AND,b,extra,0);
 out[0]=result[0];out[1]=result[1];out[2]=result[2];out[3]=result[3];
#undef XE_COLOR_OP
 return 1;
}
#endif
