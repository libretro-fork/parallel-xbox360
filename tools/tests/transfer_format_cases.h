/* Portable recipes plus independent golden/CPU-VM checks, test-only C89. */
#ifndef XE_TEST_TRANSFER_FORMAT_CASES_H
#define XE_TEST_TRANSFER_FORMAT_CASES_H
#define main xe_transfer_format_cpu_goldens
#include "core_transfer_format_test.c"
#undef main
enum { XE_FORMAT_VALID_RECORDS=24576+XE_TEST_7E3_ROUNDING_CASES,
 XE_SAMPLE_CASES=XE_FORMAT_VALID_RECORDS+3, XE_SAMPLE_OUTPUT_CASES=XE_FORMAT_VALID_RECORDS+9 };
static int xe_format_values(xe_transfer_format_emitter *e,unsigned kind,const uintptr_t r[4],uintptr_t out[8])
{
 uintptr_t input[4],packed[2],unpacked[4],v;
 unsigned format,integer,n,j;
 for(j=0;j<8;++j)out[j]=xe_tf_u(e,0);
 if(kind<14){
  format=colors[kind].format;integer=colors[kind].integer_view;
  for(j=0;j<4;++j)input[j]=integer?r[j]:xe_tf_unary(e,XE_TF_BITCAST_U32_F32,r[j]);
  n=xe_transfer_format_pack_color(e,format,integer,input,packed);
  if(!n || xe_transfer_format_unpack_color(e,format,integer,packed,unpacked)!=n)return 0;
  out[0]=packed[0];out[1]=packed[1];
  for(j=0;j<4;++j)out[2+j]=integer?unpacked[j]:xe_tf_unary(e,XE_TF_BITCAST_F32_U32,unpacked[j]);
 }else if(kind==14){
  v=xe_tf_unary(e,XE_TF_BITCAST_U32_F32,r[0]);
  out[0]=xe_transfer_format_pack7e3(e,v);
 }else if(kind==15){
  v=xe_transfer_format_unpack7e3(e,r[0]);
  out[0]=xe_tf_unary(e,XE_TF_BITCAST_F32_U32,v);
  out[1]=xe_transfer_format_pack7e3(e,v);
 }else if(kind==16 || kind==17){
  v=xe_tf_unary(e,XE_TF_BITCAST_U32_F32,r[0]);
  for(j=0;j<2;++j){
   packed[j]=xe_transfer_format_pack20e4(e,v,j,kind-16);out[j]=packed[j];
   out[2+j]=xe_tf_unary(e,XE_TF_BITCAST_F32_U32,xe_transfer_format_unpack20e4(e,packed[j],kind-16));
  }
 }else if(kind==18){
  v=xe_tf_gamma_byte(e,r[0]);
  out[0]=xe_tf_unary(e,XE_TF_BITCAST_F32_U32,v);
  out[1]=xe_tf_unary(e,XE_TF_BITCAST_F32_U32,xe_transfer_format_gamma_encode(e,v));
 }else if(kind==19){
  static const uint32_t raw_constants[]={0,0x80000000,1,0x80000001,0x7fc12345,0x7fa12345,0xff800000};
  out[0]=xe_tf_unary(e,XE_TF_BITCAST_U32_F32,r[0]);
  out[0]=xe_tf_unary(e,XE_TF_BITCAST_F32_U32,out[0]);
  for(j=1;j<8;++j)out[j]=xe_tf_unary(e,XE_TF_BITCAST_F32_U32,xe_tf_op(e,XE_TF_F32_BITS,raw_constants[j-1],0,0));
 }else if(kind==20 || kind==21){
  for(j=0;j<4;++j)input[j]=xe_tf_unary(e,XE_TF_BITCAST_U32_F32,r[j]);
  if(!xe_transfer_format_direct_color(e,kind-20,21-kind,0,input,unpacked))return 0;
  for(j=0;j<4;++j)out[j]=xe_tf_unary(e,XE_TF_BITCAST_F32_U32,unpacked[j]);
 }else return 0;
 for(j=0;j<8;++j)if(!out[j])return 0;
 return !e->failed;
}
static void xe_sample_input(uint32_t *r,unsigned index,unsigned round)
{
 static const uint32_t raw[]={0,0x80000000,1,0x80000001,0x7fc12345,0xffc54321,0x7fa12345,0x7f800000,0xff800000};
 unsigned kind=(index+round*7)%22,j,n=(index/22+round*131);
 memset(r,0,32*sizeof(*r));r[0]=kind;
 if(index>=XE_FORMAT_VALID_RECORDS){r[0]=22+(index-XE_FORMAT_VALID_RECORDS)*41;return;}
 if(index>=24576){
  r[0]=14;r[1]=ref_7e3_rounding_input(index-24576);r[31]=UINT32_C(0x7e300001);return;
 }
 if(kind<14)for(j=0;j<4;++j)r[1+j]=colors[kind].source[j];
 else if(kind==14)r[1]=small[n%(sizeof(small)/sizeof(small[0]))][0];
 else if(kind==15)r[1]=n%1024;
 else if(kind==16 || kind==17){j=kind==16?n%9:9+n%5;r[1]=depths[j][0];}
 else if(kind==18)r[1]=n%256;
 else if(kind==19)r[1]=raw[n%(sizeof(raw)/sizeof(raw[0]))];
 else{for(j=0;j<3;++j)r[1+j]=bits((float)((n+j*71)%256)/255.0f);r[4]=UINT32_C(0x3f000000);}
}
static void xe_sample_expected(const uint32_t *r,uint32_t out[8])
{
 xe_transfer_format_emitter e=open_machine(0);uintptr_t ids[8],input[4];unsigned j;
 if(r[0]>=22){for(j=0;j<8;++j)out[j]=0x6d6d6d6d;return;}
 if(r[31]==UINT32_C(0x7e300001)){
  for(j=0;j<8;++j)out[j]=0;
  out[0]=ref_7e3_pack_bits(r[1]);return;
 }
 for(j=0;j<4;++j)input[j]=xe_tf_u(&e,r[1+j]);
 CHECK(xe_format_values(&e,r[0],input,ids));
 for(j=0;j<8;++j)out[j]=get(&vm,ids[j],U32).bits;
}
#undef CHECK
#endif
