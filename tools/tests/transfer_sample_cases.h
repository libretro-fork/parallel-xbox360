/* Procedural runtime sample-address inputs and independent oracle. BSD; C89. */
#ifndef XE_TEST_TRANSFER_SAMPLE_CASES_H
#define XE_TEST_TRANSFER_SAMPLE_CASES_H
#include <stdint.h>
enum {XE_SAMPLE_CASES=49155,XE_SAMPLE_OUTPUT_CASES=49161};
static uint32_t xe_sample_hash(uint32_t n) {
 n^=n>>16;n*=0x7feb352du;n^=n>>15;n*=0x846ca68bu;return n^(n>>16);
}
static void xe_sample_input(uint32_t *r,unsigned index,unsigned round) {
 static const uint32_t edges[]={0,1,2,3,31,32,8191,0x7fffffffu,0x80000000u,0xffffffffu};
 unsigned t=(index+round*17)%49152,which=t%32,i;
 t/=32;r[5]=t%16+1;t/=16;r[6]=t%16+1;t/=16;r[4]=t%2;t/=2;r[3]=t%3;
 if(index>=49152) {
  static const uint32_t large[]={0x80000000u,0xffffffffu,0x10001u};
  r[5]=large[index-49152];r[6]=large[2-(index-49152)];
 }
 r[0]=which<10?edges[which]:xe_sample_hash(index+round*7919);
 r[1]=which<10?edges[9-which]:xe_sample_hash(index+round*13+0x7763u);
 r[2]=r[3]==2?(which&3):r[3]==1?(r[4]?(which&1):(which&1)*3):0;
 r[7]=(uint32_t)(UINT64_C(0x100000000)/r[5]);
 r[8]=(uint32_t)(UINT64_C(0x100000000)/r[6]);
 r[13]=(r[3]+index%3)%3;r[14]=r[4]^1;
 r[15]=index<49152?17-r[6]:r[5];r[16]=index<49152?17-r[5]:r[6];
 r[9]=which<10?edges[which]:xe_sample_hash(index+round*2731+0x81u);
 r[10]=which<10?edges[9-which]:xe_sample_hash(index+round*19+0x6631u);
 r[11]=(index+round)%r[15];r[12]=(index*3+round)%r[16];
 for(i=17;i<32;++i)r[i]=0x913b7000u+i;
}
static void xe_sample_expected(const uint32_t *r,uint32_t out[7]) {
 uint32_t x=r[0]/r[5],y=r[1]/r[6],sample=r[2],u,v,s;
 if(r[3]==2) {
  u=(x&1)|((sample&1)<<1)|((x>>1)<<2);
  v=(y&1)|(sample&2)|((y>>1)<<2);
 } else if(r[3]==1) {
  s=r[4]?(sample^1):(sample>>1);
  u=(x&0xfffffffdu)|((s&1)<<1);v=(y&1)|(x&2)|((y>>1)<<2);
 } else {u=x;v=y;}
 out[0]=u;out[1]=v;out[2]=r[0]%r[5];out[3]=r[1]%r[6];
 u=r[9];v=r[10];
 if(r[13]==2) {
  x=(u&1)|((u>>2)<<1);y=(v&1)|((v>>2)<<1);s=((u>>1)&1)|(v&2);
 } else if(r[13]==1) {
  x=(u&0xfffffffdu)|(v&2);y=(v&1)|((v>>2)<<1);
  s=(u>>1)&1;s=r[14]?(s^1):s*3;
 } else {x=u;y=v;s=0;}
 out[4]=x*r[15]+r[11];out[5]=y*r[16]+r[12];out[6]=s;
}
#endif
