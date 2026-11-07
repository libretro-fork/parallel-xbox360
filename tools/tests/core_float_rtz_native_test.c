/* Actual shader modes/packed ALU inputs, without a game or frontend. C89. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"native rtz line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
size_t xe_rtz_native_spirv(const uint32_t*,size_t,unsigned,unsigned,unsigned,uint32_t*,size_t);
int xe_rtz_native_interpret(const uint32_t*,const uint32_t*,unsigned,uint32_t*);
static uint32_t binary[65536],pixel_baseline[65536];
static unsigned scan(size_t size,unsigned *rtz,unsigned *rte,unsigned vertex) {
   size_t at=5;unsigned integers=0,no_contraction=0;
   *rtz=*rte=0;CHECK(size>=5 && binary[0]==UINT32_C(0x07230203));
   while(at<size) {
      unsigned n=binary[at]>>16,op=binary[at]&65535;
      CHECK(n && n<=size-at);
      if(op==16 && n==4 && binary[at+3]==32) {
         *rtz+=binary[at+2]==4463;*rte+=binary[at+2]==4462;
      }
      if(op==17 && n==2)CHECK(binary[at+1]!=11 && binary[at+1]!=10);
      if(op==71 && n==3 && binary[at+2]==42)++no_contraction;
      integers+=op==128 || op==130 || op==132;
      at+=n;
   }
   CHECK(at==size && *rtz+*rte==(vertex==2?0u:1u));if(vertex)CHECK(no_contraction);return integers;
}
static void packed(uint32_t code[6],unsigned pixel,unsigned mul) {
   code[0]=0x1001;code[1]=0x2000;code[2]=0;
   code[3]=(pixel?0:62)|(1u<<15)|(15u<<16)|(50u<<26);
   code[4]=0;code[5]=(1u<<8)|((mul==2?8:mul)<<24)|(1u<<31)|(1u<<30);
}
int main(int argc,char** argv) {
   static const uint32_t inputs[3][8]={
      {0x3f800000,0xbf800000,0x3f800000,0x7f7fffff,0x33c00000,0xb3c00000,0x80800000,0x7f7fffff},
      {0x3f800801,0xbf800801,0x7f7fffff,0x3f800000,0x3f800801,0x3f800801,0x40000000,0x3f800000},
      {0x80800000,0x00800000,0x80000000,0x80000001,0,0,0,0}};
   static const uint32_t expected[2][3][4]={
      {{0x3f800000,0xbf800000,0x3f7fffff,0x7f7fffff},{0x3f801002,0xbf801002,0x7f7fffff,0x3f800000},{0x3f7fffff,0x00800000,0,0}},
      {{0x3f800001,0xbf800001,0x3f800000,0x7f800000},{0x3f801003,0xbf801003,0x7f800000,0x3f800000},{0x3f800000,0x00800000,0,0}}};
   uint32_t code[6],out[4];size_t size,pixel_size=0;
   unsigned pixel,mul,hardware,ftz,rtz,rte,integers,soft=0,fast=0;
   char path[256];FILE* file;
   if(argc>1)CHECK(strlen(argv[1])<180);
   for(pixel=0;pixel<2;++pixel)for(mul=0;mul<3;++mul) {
      packed(code,pixel,mul);
      CHECK(xe_rtz_native_interpret(code,inputs[mul],pixel,out));
      if(memcmp(out,expected[pixel][mul],sizeof(out))) {
         fprintf(stderr,"stage %u op %u actual %08x %08x %08x %08x expected %08x %08x %08x %08x\n",
            pixel,mul,(unsigned)out[0],(unsigned)out[1],(unsigned)out[2],(unsigned)out[3],
            (unsigned)expected[pixel][mul][0],(unsigned)expected[pixel][mul][1],(unsigned)expected[pixel][mul][2],(unsigned)expected[pixel][mul][3]);
         CHECK(0);
      }
      for(ftz=0;ftz<2;++ftz)for(hardware=0;hardware<4;++hardware) {
         size=xe_rtz_native_spirv(code,6,pixel,hardware,ftz,binary,65536);CHECK(size);
         integers=scan(size,&rtz,&rte,!pixel);
         CHECK(rtz==(unsigned)(!pixel && (hardware&1) && ftz) && rte==(unsigned)!rtz);
         if(!pixel && ftz) {if(hardware&1)fast=integers;else soft=integers;}
         if(pixel) {
            if(!(hardware&1)){pixel_size=size;memcpy(pixel_baseline,binary,size*4);}
            else CHECK(size==pixel_size && !memcmp(binary,pixel_baseline,size*4));
         }
         if(argc>1) {
            sprintf(path,"%s/stage%u-mul%u-hw%u-ftz%u.spv",argv[1],pixel,mul,hardware,ftz);
            file=fopen(path,"wb");CHECK(file && fwrite(binary,4,size,file)==size && !fclose(file));
         }
      }
      if(!pixel)CHECK(soft>fast);
   }
   packed(code,0,1);
   size=xe_rtz_native_spirv(code,6,0,4,0,binary,65536);CHECK(size);
   scan(size,&rtz,&rte,2);CHECK(binary[1]==0x10000);
   if(argc>1) {
      sprintf(path,"%s/legacy-vulkan10.spv",argv[1]);file=fopen(path,"wb");
      CHECK(file && fwrite(binary,4,size,file)==size && !fclose(file));
   }
   puts("PASS: software RTZ on SPIR-V 1.0 without optional float controls or shader int64/float64");
   printf("PASS: actual packed VS/PS add+mul+frac interpreter boundaries; Vulkan RTZ/RTE capability matrix; pixel bytes unchanged; integer recipe %u vs HW %u ops\n",soft,fast);
   return 0;
}
