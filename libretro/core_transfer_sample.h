/* Runtime sample addressing for prepared transfer shaders. BSD; MSVC C89.
 * Values are borrowed native IR IDs, never pointers to temporary payloads.
 * No allocations, float operations, shader int64 or variable division.
 * This is shared construction logic, not a fallback rendering implementation. */
#ifndef XE_CORE_TRANSFER_SAMPLE_H
#define XE_CORE_TRANSFER_SAMPLE_H
#include "core_gpu_divide.h"

/* The first eight operations deliberately match xe_gpu_divide_op. */
enum xe_transfer_sample_op {
 XE_TRANSFER_SAMPLE_CONST=XE_GPU_DIV_CONST,
 XE_TRANSFER_SAMPLE_MUL_HIGH=XE_GPU_DIV_MUL_HIGH, XE_TRANSFER_SAMPLE_EQ=XE_GPU_DIV_EQ,
 XE_TRANSFER_SAMPLE_MUL=XE_GPU_DIV_MUL, XE_TRANSFER_SAMPLE_SUB=XE_GPU_DIV_SUB,
 XE_TRANSFER_SAMPLE_GE=XE_GPU_DIV_GE,
 XE_TRANSFER_SAMPLE_ADD=XE_GPU_DIV_ADD, XE_TRANSFER_SAMPLE_SELECT=XE_GPU_DIV_SELECT, XE_TRANSFER_SAMPLE_AND,
 XE_TRANSFER_SAMPLE_OR, XE_TRANSFER_SAMPLE_XOR, XE_TRANSFER_SAMPLE_SHR,
 XE_TRANSFER_SAMPLE_SHL
};
typedef uintptr_t (*xe_transfer_sample_emit_fn)(void *,
 enum xe_transfer_sample_op, uintptr_t, uintptr_t, uintptr_t);
typedef struct xe_transfer_sample_emitter {
 xe_transfer_sample_emit_fn emit;
 void *context;
} xe_transfer_sample_emitter;
typedef struct xe_transfer_sample_parameters {
 /* log2 guest sample count: 0,1,2. Native2 is 0 or1. Scale is nonzero.
  * Reciprocal is CPU-prepared with xe_gpu_divide_prepare for each scale. */
 uintptr_t msaa, native2, scale_x, scale_y, reciprocal_x, reciprocal_y;
} xe_transfer_sample_parameters;
typedef struct xe_transfer_sample_coordinates {
 uintptr_t x,y,sample,sub_x,sub_y;
} xe_transfer_sample_coordinates;
typedef struct xe_transfer_sample_config {
 uint32_t msaa,native2,scale_x,scale_y,reciprocal_x,reciprocal_y;
} xe_transfer_sample_config;
/* CPU preparation before publication: no invalid dynamic division inputs.
 * Backend resolution limits are checked by the caller. All u32 divisors work. */
static INLINE int xe_transfer_sample_prepare(xe_transfer_sample_config *out,
 uint32_t msaa,uint32_t native2,uint32_t scale_x,uint32_t scale_y) {
 xe_transfer_sample_config value;
 if(!out || msaa>2 || native2>1 || !scale_x || !scale_y)return 0;
 value.msaa=msaa;value.native2=native2;value.scale_x=scale_x;value.scale_y=scale_y;
 if(!xe_gpu_divide_prepare(scale_x,&value.reciprocal_x) ||
    !xe_gpu_divide_prepare(scale_y,&value.reciprocal_y))return 0;
 *out=value;return 1;
}

static INLINE uintptr_t xe_transfer_sample_divide(void *context,
 enum xe_gpu_divide_op op,uintptr_t a,uintptr_t b,uintptr_t c) {
 xe_transfer_sample_emitter *e=(xe_transfer_sample_emitter*)context;
 return e->emit(e->context,(enum xe_transfer_sample_op)op,a,b,c);
}

/* Canonical guest sample coordinates carry resolution subpixels separately.
 * Runtime key validation belongs to the CPU metadata resolver; every input
 * here is an IR ID, including literal zero values. Failure leaves out intact. */
static INLINE int xe_transfer_sample_canonical_emit(
 xe_transfer_sample_emitter *e,const xe_transfer_sample_parameters *p,
 uintptr_t pixel_x,uintptr_t pixel_y,uintptr_t host_sample,
 xe_transfer_sample_coordinates *out) {
 xe_transfer_sample_coordinates result;
 uintptr_t zero,one,two,not_one,all,x,y,sub_x,sub_y;
 uintptr_t guest_sample,active,four,two_samples,native,a,b,c,is2,is4;
 if(!e || !e->emit || !p || !out || !pixel_x || !pixel_y || !host_sample ||
    !p->msaa || !p->native2 || !p->scale_x || !p->scale_y ||
    !p->reciprocal_x || !p->reciprocal_y)return 0;
#define XE_SAMPLE_OP(dst,op,a,b,c) do { \
 dst=e->emit(e->context,op,a,b,c);if(!dst)return 0; \
} while(0)
 XE_SAMPLE_OP(zero,XE_TRANSFER_SAMPLE_CONST,0,0,0);
 XE_SAMPLE_OP(one,XE_TRANSFER_SAMPLE_CONST,1,0,0);
 XE_SAMPLE_OP(two,XE_TRANSFER_SAMPLE_CONST,2,0,0);
 XE_SAMPLE_OP(not_one,XE_TRANSFER_SAMPLE_CONST,0xfffffffeu,0,0);
 XE_SAMPLE_OP(all,XE_TRANSFER_SAMPLE_CONST,0xffffffffu,0,0);
 if(!xe_gpu_divide_emit(xe_transfer_sample_divide,e,pixel_x,p->scale_x,
      p->reciprocal_x,&x,&sub_x) ||
    !xe_gpu_divide_emit(xe_transfer_sample_divide,e,pixel_y,p->scale_y,
      p->reciprocal_y,&y,&sub_y))return 0;
 /* Normalize sample routing to u32 flags. Do not select between whole
  * packed coordinates: compute their shared bit layout once instead. */
 XE_SAMPLE_OP(is2,XE_TRANSFER_SAMPLE_EQ,p->msaa,one,0);
 XE_SAMPLE_OP(is4,XE_TRANSFER_SAMPLE_EQ,p->msaa,two,0);
 XE_SAMPLE_OP(a,XE_TRANSFER_SAMPLE_GE,p->msaa,one,0);
 XE_SAMPLE_OP(active,XE_TRANSFER_SAMPLE_SELECT,a,one,zero);
 XE_SAMPLE_OP(four,XE_TRANSFER_SAMPLE_SELECT,is4,one,zero);
 XE_SAMPLE_OP(two_samples,XE_TRANSFER_SAMPLE_SELECT,is2,one,zero);
 XE_SAMPLE_OP(native,XE_TRANSFER_SAMPLE_MUL,two_samples,p->native2,0);
 XE_SAMPLE_OP(a,XE_TRANSFER_SAMPLE_SUB,two_samples,native,0);
 XE_SAMPLE_OP(b,XE_TRANSFER_SAMPLE_SHR,host_sample,a,0);
 XE_SAMPLE_OP(guest_sample,XE_TRANSFER_SAMPLE_XOR,b,native,0);
 XE_SAMPLE_OP(a,XE_TRANSFER_SAMPLE_AND,x,not_one,0);
 XE_SAMPLE_OP(a,XE_TRANSFER_SAMPLE_SHL,a,four,0);
 XE_SAMPLE_OP(b,XE_TRANSFER_SAMPLE_AND,x,one,0);
 XE_SAMPLE_OP(c,XE_TRANSFER_SAMPLE_OR,a,b,0);
 XE_SAMPLE_OP(a,XE_TRANSFER_SAMPLE_SHL,active,one,0);
 XE_SAMPLE_OP(a,XE_TRANSFER_SAMPLE_XOR,a,all,0);
 XE_SAMPLE_OP(c,XE_TRANSFER_SAMPLE_AND,c,a,0);
 XE_SAMPLE_OP(b,XE_TRANSFER_SAMPLE_AND,guest_sample,active,0);
 XE_SAMPLE_OP(b,XE_TRANSFER_SAMPLE_SHL,b,one,0);
 XE_SAMPLE_OP(result.x,XE_TRANSFER_SAMPLE_OR,c,b,0);
 XE_SAMPLE_OP(a,XE_TRANSFER_SAMPLE_AND,y,not_one,0);
 XE_SAMPLE_OP(a,XE_TRANSFER_SAMPLE_SHL,a,active,0);
 XE_SAMPLE_OP(b,XE_TRANSFER_SAMPLE_AND,y,one,0);
 XE_SAMPLE_OP(c,XE_TRANSFER_SAMPLE_OR,a,b,0);
 XE_SAMPLE_OP(a,XE_TRANSFER_SAMPLE_AND,x,two,0);
 XE_SAMPLE_OP(a,XE_TRANSFER_SAMPLE_MUL,a,two_samples,0);
 XE_SAMPLE_OP(c,XE_TRANSFER_SAMPLE_OR,c,a,0);
 XE_SAMPLE_OP(b,XE_TRANSFER_SAMPLE_AND,host_sample,two,0);
 XE_SAMPLE_OP(b,XE_TRANSFER_SAMPLE_MUL,b,four,0);
 XE_SAMPLE_OP(result.y,XE_TRANSFER_SAMPLE_OR,c,b,0);
 result.sample=zero;result.sub_x=sub_x;result.sub_y=sub_y;
 *out=result;
#undef XE_SAMPLE_OP
 return 1;
}

static INLINE int xe_transfer_sample_decanonical_emit(
 xe_transfer_sample_emitter *e,const xe_transfer_sample_parameters *p,
 const xe_transfer_sample_coordinates *in,xe_transfer_sample_coordinates *out) {
 xe_transfer_sample_coordinates result;
 uintptr_t zero,one,two,not_three,active,four,two_samples,none,s2,s4;
 uintptr_t a,b,c,native,is4,x,y,sample;
 if(!e || !e->emit || !p || !in || !out || !in->x || !in->y ||
    !in->sub_x || !in->sub_y || !p->msaa || !p->native2 ||
    !p->scale_x || !p->scale_y)return 0;
#define XE_SAMPLE_OP(dst,op,a,b,c) do { \
 dst=e->emit(e->context,op,a,b,c);if(!dst)return 0; \
} while(0)
 XE_SAMPLE_OP(zero,XE_TRANSFER_SAMPLE_CONST,0,0,0);
 XE_SAMPLE_OP(one,XE_TRANSFER_SAMPLE_CONST,1,0,0);
 XE_SAMPLE_OP(two,XE_TRANSFER_SAMPLE_CONST,2,0,0);
 XE_SAMPLE_OP(not_three,XE_TRANSFER_SAMPLE_CONST,0xfffffffcu,0,0);
 /* Deinterleave once using normalized u32 flags. Whole-coordinate selects
  * keep several competing bit layouts live through a texture fetch. */
 XE_SAMPLE_OP(is4,XE_TRANSFER_SAMPLE_EQ,p->msaa,two,0);
 XE_SAMPLE_OP(a,XE_TRANSFER_SAMPLE_GE,p->msaa,one,0);
 XE_SAMPLE_OP(active,XE_TRANSFER_SAMPLE_SELECT,a,one,zero);
 XE_SAMPLE_OP(four,XE_TRANSFER_SAMPLE_SELECT,is4,one,zero);
 XE_SAMPLE_OP(two_samples,XE_TRANSFER_SAMPLE_SUB,active,four,0);
 XE_SAMPLE_OP(none,XE_TRANSFER_SAMPLE_SUB,one,active,0);
 XE_SAMPLE_OP(a,XE_TRANSFER_SAMPLE_AND,in->x,not_three,0);
 XE_SAMPLE_OP(a,XE_TRANSFER_SAMPLE_SHR,a,four,0);
 XE_SAMPLE_OP(b,XE_TRANSFER_SAMPLE_AND,in->x,one,0);
 XE_SAMPLE_OP(x,XE_TRANSFER_SAMPLE_OR,a,b,0);
 XE_SAMPLE_OP(a,XE_TRANSFER_SAMPLE_AND,in->x,two,0);
 XE_SAMPLE_OP(a,XE_TRANSFER_SAMPLE_MUL,a,none,0);
 XE_SAMPLE_OP(x,XE_TRANSFER_SAMPLE_OR,x,a,0);
 XE_SAMPLE_OP(a,XE_TRANSFER_SAMPLE_AND,in->y,two,0);
 XE_SAMPLE_OP(a,XE_TRANSFER_SAMPLE_MUL,a,two_samples,0);
 XE_SAMPLE_OP(x,XE_TRANSFER_SAMPLE_OR,x,a,0);
 XE_SAMPLE_OP(a,XE_TRANSFER_SAMPLE_AND,in->y,not_three,0);
 XE_SAMPLE_OP(a,XE_TRANSFER_SAMPLE_SHR,a,active,0);
 XE_SAMPLE_OP(b,XE_TRANSFER_SAMPLE_AND,in->y,one,0);
 XE_SAMPLE_OP(y,XE_TRANSFER_SAMPLE_OR,a,b,0);
 XE_SAMPLE_OP(a,XE_TRANSFER_SAMPLE_AND,in->y,two,0);
 XE_SAMPLE_OP(a,XE_TRANSFER_SAMPLE_MUL,a,none,0);
 XE_SAMPLE_OP(y,XE_TRANSFER_SAMPLE_OR,y,a,0);
 XE_SAMPLE_OP(a,XE_TRANSFER_SAMPLE_SHR,in->x,one,0);
 XE_SAMPLE_OP(a,XE_TRANSFER_SAMPLE_AND,a,one,0);
 XE_SAMPLE_OP(b,XE_TRANSFER_SAMPLE_XOR,a,one,0);
 XE_SAMPLE_OP(c,XE_TRANSFER_SAMPLE_SHL,a,one,0);
 XE_SAMPLE_OP(c,XE_TRANSFER_SAMPLE_OR,c,a,0);
 XE_SAMPLE_OP(native,XE_TRANSFER_SAMPLE_EQ,p->native2,one,0);
 XE_SAMPLE_OP(s2,XE_TRANSFER_SAMPLE_SELECT,native,b,c);
 XE_SAMPLE_OP(b,XE_TRANSFER_SAMPLE_AND,in->y,two,0);
 XE_SAMPLE_OP(s4,XE_TRANSFER_SAMPLE_OR,a,b,0);
 XE_SAMPLE_OP(a,XE_TRANSFER_SAMPLE_MUL,s2,two_samples,0);
 XE_SAMPLE_OP(b,XE_TRANSFER_SAMPLE_MUL,s4,four,0);
 XE_SAMPLE_OP(sample,XE_TRANSFER_SAMPLE_OR,a,b,0);
 XE_SAMPLE_OP(a,XE_TRANSFER_SAMPLE_MUL,x,p->scale_x,0);
 XE_SAMPLE_OP(result.x,XE_TRANSFER_SAMPLE_ADD,a,in->sub_x,0);
 XE_SAMPLE_OP(b,XE_TRANSFER_SAMPLE_MUL,y,p->scale_y,0);
 XE_SAMPLE_OP(result.y,XE_TRANSFER_SAMPLE_ADD,b,in->sub_y,0);
 result.sample=sample;result.sub_x=in->sub_x;result.sub_y=in->sub_y;
 *out=result;
#undef XE_SAMPLE_OP
 return 1;
}
#endif
