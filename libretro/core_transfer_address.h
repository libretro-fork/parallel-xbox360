/* Physical EDRAM source routing for prepared transfer shaders. BSD; C89.
 * No image access or format conversion here. Pixels and sample are defined
 * u32 IR IDs. CPU metadata is validated before publication. */
#ifndef XE_CORE_TRANSFER_ADDRESS_H
#define XE_CORE_TRANSFER_ADDRESS_H
#include "core_transfer_sample.h"
enum xe_transfer_address_word {
 XE_ADDR_D_MSAA,XE_ADDR_D_NATIVE2,XE_ADDR_D_SX,XE_ADDR_D_SY,XE_ADDR_D_RX,XE_ADDR_D_RY,
 XE_ADDR_S_MSAA,XE_ADDR_S_NATIVE2,XE_ADDR_S_SX,XE_ADDR_S_SY,
 XE_ADDR_D_WIDE,XE_ADDR_S_WIDE,XE_ADDR_D_TW,XE_ADDR_D_TH,XE_ADDR_D_TRX,XE_ADDR_D_TRY,
 XE_ADDR_S_TW,XE_ADDR_S_TH,XE_ADDR_D_PITCH,XE_ADDR_S_PITCH,XE_ADDR_S_PR,
 XE_ADDR_DELTA,XE_ADDR_SWAP,XE_ADDR_WORDS
};
typedef struct xe_transfer_address_config { uint32_t word[XE_ADDR_WORDS]; } xe_transfer_address_config;
typedef struct xe_transfer_address_parameters { uintptr_t word[XE_ADDR_WORDS]; } xe_transfer_address_parameters;
typedef struct xe_transfer_address_key {
 uint32_t dest_msaa,source_msaa,dest_native2,source_native2;
 uint32_t dest_scale_x,dest_scale_y,source_scale_x,source_scale_y;
 uint32_t dest_wide,source_wide,dest_color,source_color,dest_pitch,source_pitch;
 /* Destination base minus source base, in 32-bpp EDRAM tiles. */
 int32_t dest_to_source_base;
} xe_transfer_address_key;
typedef struct xe_transfer_address_result {
 uintptr_t x,y,sample,second_x,half,two_loads,tile;
 /* Original destination-space values are also lent to host-depth routing,
  * so that a later stage need not redo these divisions. */
 uintptr_t dest_tile,dest_x,dest_y;
} xe_transfer_address_result;
typedef char xe_transfer_address_config_size[(sizeof(xe_transfer_address_config)==92)?1:-1];
static INLINE int xe_transfer_address_key_valid(const xe_transfer_address_key *key) {
 if(!key || key->dest_msaa>2 || key->source_msaa>2 ||
    key->dest_native2>1 || key->source_native2>1 ||
    key->dest_native2!=key->source_native2 || key->dest_wide>1 || key->source_wide>1 ||
    key->dest_color>1 || key->source_color>1 || (!key->dest_color && key->dest_wide) ||
    (!key->source_color && key->source_wide) || !key->dest_scale_x || !key->dest_scale_y ||
    !key->source_scale_x || !key->source_scale_y ||
    key->dest_scale_x>UINT32_MAX/80 || key->source_scale_x>UINT32_MAX/80 ||
    key->dest_scale_y>UINT32_MAX/16 || key->source_scale_y>UINT32_MAX/16 ||
    !key->dest_pitch || key->dest_pitch>1023 || !key->source_pitch || key->source_pitch>1023 ||
    key->dest_to_source_base < -2047 || key->dest_to_source_base > 2047)return 0;
 /* The existing core has one native2 feature policy and native or one shared
  * scaled class, not per-image native2 policies or arbitrary
  * resampling between unrelated scale pairs. */
 if(!((key->dest_scale_x==key->source_scale_x && key->dest_scale_y==key->source_scale_y) ||
      (key->dest_scale_x==1 && key->dest_scale_y==1) ||
      (key->source_scale_x==1 && key->source_scale_y==1)))return 0;
 return 1;
}
static INLINE int xe_transfer_address_prepare(xe_transfer_address_config *out,
 const xe_transfer_address_key *key) {
 /* key is borrowed for this call and must not overlap the final packet out. */
 uint32_t dw,dh,sw,sh;
 if(!out || !xe_transfer_address_key_valid(key))return 0;
 dw=(80*key->dest_scale_x)>>(key->dest_wide+(key->dest_msaa==2));
 dh=(16*key->dest_scale_y)>>(uint32_t)(key->dest_msaa!=0);
 sw=(80*key->source_scale_x)>>(key->source_wide+(key->source_msaa==2));
 sh=(16*key->source_scale_y)>>(uint32_t)(key->source_msaa!=0);
 /* All rejection precedes output writes; each divisor below is nonzero. */
 out->word[XE_ADDR_D_MSAA]=key->dest_msaa;out->word[XE_ADDR_D_NATIVE2]=key->dest_native2;
 out->word[XE_ADDR_D_SX]=key->dest_scale_x;out->word[XE_ADDR_D_SY]=key->dest_scale_y;
 xe_gpu_divide_prepare(key->dest_scale_x,&out->word[XE_ADDR_D_RX]);
 xe_gpu_divide_prepare(key->dest_scale_y,&out->word[XE_ADDR_D_RY]);
 out->word[XE_ADDR_S_MSAA]=key->source_msaa;out->word[XE_ADDR_S_NATIVE2]=key->source_native2;
 out->word[XE_ADDR_S_SX]=key->source_scale_x;out->word[XE_ADDR_S_SY]=key->source_scale_y;
 out->word[XE_ADDR_D_WIDE]=key->dest_wide;out->word[XE_ADDR_S_WIDE]=key->source_wide;
 out->word[XE_ADDR_D_TW]=dw;out->word[XE_ADDR_D_TH]=dh;
 xe_gpu_divide_prepare(dw,&out->word[XE_ADDR_D_TRX]);
 xe_gpu_divide_prepare(dh,&out->word[XE_ADDR_D_TRY]);
 out->word[XE_ADDR_S_TW]=sw;out->word[XE_ADDR_S_TH]=sh;
 out->word[XE_ADDR_D_PITCH]=key->dest_pitch;out->word[XE_ADDR_S_PITCH]=key->source_pitch;
 xe_gpu_divide_prepare(key->source_pitch,&out->word[XE_ADDR_S_PR]);
 out->word[XE_ADDR_DELTA]=(uint32_t)key->dest_to_source_base;
 out->word[XE_ADDR_SWAP]=key->dest_color!=key->source_color;
 return 1;
}
static INLINE int xe_transfer_address_emit(xe_transfer_sample_emitter *e,
 const xe_transfer_address_parameters *p,uintptr_t pixel_x,uintptr_t pixel_y,
 uintptr_t sample,xe_transfer_address_result *out) {
 xe_transfer_sample_parameters dest,source;xe_transfer_sample_coordinates canonical,local;
 xe_transfer_address_result result;
 uintptr_t zero,one,mask,tx,ty,lx,ly,a,b,c,left,right,half_width,high;
 unsigned i;
 if(!e || !e->emit || !p || !out || !pixel_x || !pixel_y || !sample)return 0;
 for(i=0;i<XE_ADDR_WORDS;++i)if(!p->word[i])return 0;
#define P(n) p->word[XE_ADDR_##n]
#define OP(dst,op,a,b,c) do { \
 dst=e->emit(e->context,op,a,b,c);if(!dst)return 0; \
} while(0)
 OP(zero,XE_TRANSFER_SAMPLE_CONST,0,0,0);OP(one,XE_TRANSFER_SAMPLE_CONST,1,0,0);
 OP(mask,XE_TRANSFER_SAMPLE_CONST,2047,0,0);
 if(!xe_gpu_divide_emit(xe_transfer_sample_divide,e,pixel_x,P(D_TW),P(D_TRX),&tx,&lx) ||
    !xe_gpu_divide_emit(xe_transfer_sample_divide,e,pixel_y,P(D_TH),P(D_TRY),&ty,&ly))return 0;
 result.dest_x=lx;result.dest_y=ly;
 OP(a,XE_TRANSFER_SAMPLE_MUL,ty,P(D_PITCH),0);
 OP(result.dest_tile,XE_TRANSFER_SAMPLE_ADD,a,tx,0);
 OP(a,XE_TRANSFER_SAMPLE_ADD,result.dest_tile,P(DELTA),0);
 OP(result.tile,XE_TRANSFER_SAMPLE_AND,a,mask,0);
 dest.msaa=P(D_MSAA);dest.native2=P(D_NATIVE2);dest.scale_x=P(D_SX);dest.scale_y=P(D_SY);
 dest.reciprocal_x=P(D_RX);dest.reciprocal_y=P(D_RY);
 if(!xe_transfer_sample_canonical_emit(e,&dest,lx,ly,sample,&canonical))return 0;
 /* Equivalent to the old rescale-before-canonicalize path, without undoing
  * and then redoing the same source-scale division. */
 OP(a,XE_TRANSFER_SAMPLE_EQ,P(D_SX),P(S_SX),0);
 OP(b,XE_TRANSFER_SAMPLE_SHR,P(S_SX),one,0);
 OP(canonical.sub_x,XE_TRANSFER_SAMPLE_SELECT,a,canonical.sub_x,b);
 OP(a,XE_TRANSFER_SAMPLE_EQ,P(D_SY),P(S_SY),0);
 OP(b,XE_TRANSFER_SAMPLE_SHR,P(S_SY),one,0);
 OP(canonical.sub_y,XE_TRANSFER_SAMPLE_SELECT,a,canonical.sub_y,b);
 OP(a,XE_TRANSFER_SAMPLE_SUB,one,P(S_WIDE),0);
 OP(left,XE_TRANSFER_SAMPLE_MUL,a,P(D_WIDE),0);
 OP(a,XE_TRANSFER_SAMPLE_SUB,one,P(D_WIDE),0);
 OP(right,XE_TRANSFER_SAMPLE_MUL,a,P(S_WIDE),0);
 OP(a,XE_TRANSFER_SAMPLE_AND,canonical.x,one,0);
 OP(result.half,XE_TRANSFER_SAMPLE_MUL,a,right,0);
 OP(a,XE_TRANSFER_SAMPLE_SHL,canonical.x,left,0);
 OP(canonical.x,XE_TRANSFER_SAMPLE_SHR,a,right,0);
 source.msaa=P(S_MSAA);source.native2=P(S_NATIVE2);source.scale_x=P(S_SX);source.scale_y=P(S_SY);
 source.reciprocal_x=zero;source.reciprocal_y=zero;
 if(!xe_transfer_sample_decanonical_emit(e,&source,&canonical,&local))return 0;
 OP(half_width,XE_TRANSFER_SAMPLE_SHR,P(S_TW),one,0);
 OP(high,XE_TRANSFER_SAMPLE_GE,local.x,half_width,0);
 OP(a,XE_TRANSFER_SAMPLE_SUB,zero,half_width,0);
 OP(b,XE_TRANSFER_SAMPLE_SELECT,high,a,half_width);
 OP(b,XE_TRANSFER_SAMPLE_MUL,b,P(SWAP),0);
 OP(local.x,XE_TRANSFER_SAMPLE_ADD,local.x,b,0);
 if(!xe_gpu_divide_emit(xe_transfer_sample_divide,e,result.tile,P(S_PITCH),P(S_PR),&ty,&tx))return 0;
 OP(a,XE_TRANSFER_SAMPLE_MUL,tx,P(S_TW),0);
 OP(result.x,XE_TRANSFER_SAMPLE_ADD,a,local.x,0);
 OP(c,XE_TRANSFER_SAMPLE_MUL,ty,P(S_TH),0);
 OP(result.y,XE_TRANSFER_SAMPLE_ADD,c,local.y,0);
 OP(result.second_x,XE_TRANSFER_SAMPLE_ADD,result.x,P(S_SX),0);
 result.sample=local.sample;result.two_loads=left;
 *out=result;
#undef OP
#undef P
 return 1;
}
#endif
