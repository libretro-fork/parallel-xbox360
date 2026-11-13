/* Compact metadata for a generic ownership-transfer program. BSD; MSVC C89.
 * Configuration is immutable per renderer. Packets fill final command storage.
 * This is routing infrastructure, not an implemented rendering fallback. */
#ifndef XE_CORE_TRANSFER_ADDRESS_PACKET_H
#define XE_CORE_TRANSFER_ADDRESS_PACKET_H
#include "core_transfer_address.h"
#include <stddef.h>

enum xe_transfer_address_packet_word {
 XE_AP_ADDRESS,XE_AP_GEOMETRY,XE_AP_DEST_RX,XE_AP_DEST_RY,XE_AP_SOURCE_PR,
 XE_AP_SAMPLE,XE_AP_WORDS
};
typedef struct xe_transfer_address_packet_config {
 uint32_t scale_x,scale_y,native2;
} xe_transfer_address_packet_config;
typedef struct xe_transfer_address_packet {
 uint32_t address,geometry,dest_reciprocal_x,dest_reciprocal_y;
 uint32_t source_pitch_reciprocal,sample;
} xe_transfer_address_packet;
typedef char xe_transfer_address_packet_size[sizeof(xe_transfer_address_packet)==24?1:-1];
typedef char xe_transfer_address_packet_sample[offsetof(xe_transfer_address_packet,sample)==20?1:-1];

static INLINE int xe_transfer_address_packet_config_valid(const xe_transfer_address_packet_config *config) {
 return config && config->native2<2 && config->scale_x && config->scale_y &&
  config->scale_x<=UINT32_MAX/80 && config->scale_y<=UINT32_MAX/16;
}
/* Borrow config/key through return; neither may overlap the unpublished out. */
static INLINE int xe_transfer_address_packet_prepare(xe_transfer_address_packet *out,
 const xe_transfer_address_packet_config *config,const xe_transfer_address_key *key,uint32_t sample) {
 uint32_t dn,sn,dw,dh;
 if(!out || !xe_transfer_address_packet_config_valid(config) || !xe_transfer_address_key_valid(key) ||
    key->dest_native2!=config->native2)return 0;
 dn=key->dest_scale_x==1 && key->dest_scale_y==1;
 sn=key->source_scale_x==1 && key->source_scale_y==1;
 if((!dn && (key->dest_scale_x!=config->scale_x || key->dest_scale_y!=config->scale_y)) ||
    (!sn && (key->source_scale_x!=config->scale_x || key->source_scale_y!=config->scale_y)))return 0;
 if(key->dest_msaa==0?sample!=0:key->dest_msaa==1?
    (config->native2?sample>1:(sample!=0 && sample!=3)):sample>3)return 0;
 dw=(80*key->dest_scale_x)>>(key->dest_wide+(key->dest_msaa==2));
 dh=(16*key->dest_scale_y)>>(uint32_t)(key->dest_msaa!=0);
 /* Validation is complete. Nonzero divisors cannot fail preparation. */
 out->address=key->dest_pitch|(key->source_pitch<<10)|(((uint32_t)key->dest_to_source_base&4095u)<<20);
 out->geometry=key->dest_msaa|(key->source_msaa<<2)|(key->dest_wide<<4)|(key->source_wide<<5)|
  (key->dest_color<<6)|(key->source_color<<7)|(dn<<8)|(sn<<9);
 xe_gpu_divide_prepare(dw,&out->dest_reciprocal_x);
 xe_gpu_divide_prepare(dh,&out->dest_reciprocal_y);
 xe_gpu_divide_prepare(key->source_pitch,&out->source_pitch_reciprocal);
 out->sample=sample;return 1;
}
typedef struct xe_transfer_address_packet_source {
 const xe_transfer_address_packet_config *config;
 const xe_transfer_address_key *key;
 uint32_t sample;
} xe_transfer_address_packet_source;
static INLINE int xe_transfer_address_packet_write(void *destination,size_t bytes,void *context) {
 const xe_transfer_address_packet_source *source=(const xe_transfer_address_packet_source*)context;
 return source && bytes==sizeof(xe_transfer_address_packet) &&
  xe_transfer_address_packet_prepare((xe_transfer_address_packet*)destination,source->config,source->key,source->sample);
}
/* Expand borrowed packet SSA IDs for the existing division-free router. Scale
 * values/magic are renderer constants selected by two runtime native bits.
 * Every value is an IR ID, including literal zero. No format/pixel work here.
 * Failure leaves out intact; caller discards the incomplete module on failure. */
static INLINE int xe_transfer_address_packet_emit(xe_transfer_sample_emitter *e,
 const xe_transfer_address_packet_config *config,const uintptr_t packet[XE_AP_WORDS],
 xe_transfer_address_parameters *out) {
 xe_transfer_address_parameters p;
 uintptr_t zero,one,two,mask,sx,sy,rx,ry,n,ds,ss,a,b,c,sign;
 uint32_t magic_x,magic_y,magic_one;unsigned i;
 if(!e || !e->emit || !out || !packet || !xe_transfer_address_packet_config_valid(config))return 0;
 for(i=0;i<XE_AP_WORDS;++i)if(!packet[i])return 0;
 xe_gpu_divide_prepare(config->scale_x,&magic_x);xe_gpu_divide_prepare(config->scale_y,&magic_y);xe_gpu_divide_prepare(1,&magic_one);
#define AP_OP(dst,op,a,b,c) do {dst=e->emit(e->context,XE_TRANSFER_SAMPLE_##op,a,b,c);if(!dst)return 0;} while(0)
#define AP_CONST(dst,value) AP_OP(dst,CONST,value,0,0)
#define AP_FIELD(dst,value,shift,bits) do {AP_CONST(a,shift);AP_OP(b,SHR,value,a,0);AP_CONST(a,bits);AP_OP(dst,AND,b,a,0);} while(0)
#define AP_P(n) p.word[XE_ADDR_##n]
 AP_CONST(zero,0);AP_CONST(one,1);AP_CONST(two,2);
 AP_CONST(sx,config->scale_x);AP_CONST(sy,config->scale_y);AP_CONST(rx,magic_x);AP_CONST(ry,magic_y);AP_CONST(n,magic_one);
 AP_FIELD(AP_P(D_MSAA),packet[XE_AP_GEOMETRY],0,3);
 AP_FIELD(AP_P(S_MSAA),packet[XE_AP_GEOMETRY],2,3);
 AP_FIELD(AP_P(D_WIDE),packet[XE_AP_GEOMETRY],4,1);
 AP_FIELD(AP_P(S_WIDE),packet[XE_AP_GEOMETRY],5,1);
 AP_FIELD(ds,packet[XE_AP_GEOMETRY],8,1);AP_FIELD(ss,packet[XE_AP_GEOMETRY],9,1);
 AP_OP(ds,EQ,ds,one,0);AP_OP(ss,EQ,ss,one,0);
 AP_OP(AP_P(D_SX),SELECT,ds,one,sx);AP_OP(AP_P(D_SY),SELECT,ds,one,sy);
 AP_OP(AP_P(S_SX),SELECT,ss,one,sx);AP_OP(AP_P(S_SY),SELECT,ss,one,sy);
 AP_OP(AP_P(D_RX),SELECT,ds,n,rx);AP_OP(AP_P(D_RY),SELECT,ds,n,ry);
 AP_CONST(AP_P(D_NATIVE2),config->native2);AP_P(S_NATIVE2)=AP_P(D_NATIVE2);
 AP_CONST(mask,80);AP_OP(c,MUL,mask,AP_P(D_SX),0);AP_OP(a,EQ,AP_P(D_MSAA),two,0);AP_OP(a,SELECT,a,one,zero);
 AP_OP(b,ADD,a,AP_P(D_WIDE),0);AP_OP(AP_P(D_TW),SHR,c,b,0);
 AP_OP(c,MUL,mask,AP_P(S_SX),0);AP_OP(a,EQ,AP_P(S_MSAA),two,0);AP_OP(a,SELECT,a,one,zero);
 AP_OP(b,ADD,a,AP_P(S_WIDE),0);AP_OP(AP_P(S_TW),SHR,c,b,0);
 AP_CONST(mask,16);AP_OP(c,MUL,mask,AP_P(D_SY),0);AP_OP(a,GE,AP_P(D_MSAA),one,0);AP_OP(a,SELECT,a,one,zero);AP_OP(AP_P(D_TH),SHR,c,a,0);
 AP_OP(c,MUL,mask,AP_P(S_SY),0);AP_OP(a,GE,AP_P(S_MSAA),one,0);AP_OP(a,SELECT,a,one,zero);AP_OP(AP_P(S_TH),SHR,c,a,0);
 AP_P(D_TRX)=packet[XE_AP_DEST_RX];AP_P(D_TRY)=packet[XE_AP_DEST_RY];AP_P(S_PR)=packet[XE_AP_SOURCE_PR];
 AP_FIELD(AP_P(D_PITCH),packet[XE_AP_ADDRESS],0,1023);AP_FIELD(AP_P(S_PITCH),packet[XE_AP_ADDRESS],10,1023);
 AP_FIELD(c,packet[XE_AP_ADDRESS],20,4095);AP_CONST(sign,2048);AP_OP(a,XOR,c,sign,0);AP_OP(AP_P(DELTA),SUB,a,sign,0);
 AP_FIELD(ds,packet[XE_AP_GEOMETRY],6,1);AP_FIELD(ss,packet[XE_AP_GEOMETRY],7,1);AP_OP(AP_P(SWAP),XOR,ds,ss,0);
 *out=p;
#undef AP_P
#undef AP_FIELD
#undef AP_CONST
#undef AP_OP
 return 1;
}
#endif
