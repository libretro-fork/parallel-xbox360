/* Actual next-jump analysis/translation and CPU semantics. MSVC C89. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../libretro/core_spv_binary.h"
#include "../../libretro/core_spv_plan.h"
size_t xe_forward_spirv(const uint32_t*,size_t,unsigned,uint32_t*,size_t,unsigned*,uint32_t*);
int xe_forward_builder(unsigned,xe_spv_plan*,xe_spv_binary*,uint32_t*);
int xe_forward_interpret(const uint32_t*,unsigned,const float*,float*);
#define CHECK(x) do {if(!(x)){fprintf(stderr,"forward flow line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
static uint32_t binary[65536];
static void cf(uint32_t* code,unsigned index,uint32_t low,unsigned high) {
   uint32_t* p=code+(index/2)*3;
   if(index&1){p[1]|=low<<16;p[2]=(low>>16)|((uint32_t)high<<16);}
   else{p[0]=low;p[1]=high;}
}
static size_t scenario(uint32_t code[15],unsigned pixel,unsigned which) {
   unsigned payload=which<3 || which==5?1:(which==6?3:2);
   unsigned low=0x2001,high=0xb000;
   memset(code,0,15*sizeof(*code));
   if(which==1){low=1;high=0xb444;}
   else if(which==2)low=0x4001;
   else if(which==4 || which==7)low=0x2002;
   else if(which==5){low=0;high=0xa000;}
   else if(which==6)high=0x9000;
   cf(code,0,low,high);
   if(which==3) {cf(code,1,0x1002,0x2000);cf(code,2,0x2001,0xb000);}
   else if(which==4 || which==7) {cf(code,1,0x1002,which==7?0x5400:0x1000);cf(code,2,0x1003,0x2000);}
   else if(which==6) {cf(code,1,0x1003,0x1000);cf(code,2,0,0xa000);cf(code,3,0x1004,0x2000);}
   else cf(code,1,0x1001,0x2000);
   if(which==4 || which==6 || which==7) {
      code[payload*3]=0xc80f0000;code[payload*3+1]=0;code[payload*3+2]=0xc0000100;
      ++payload;
   }
   code[payload*3]=(pixel?0:62)|(1u<<15)|(15u<<16)|(50u<<26);
   code[payload*3+1]=0;code[payload*3+2]=0xc2000000;
   return (payload+1)*3;
}
int main(int argc,char**argv) {
   static const float inputs[8]={2.25f,3.5f,-4.0f,5.0f,1,1,1,1};
   uint32_t code[15],bools;float out[4],expected[4];unsigned pixel,which,labels,switches;
   size_t count,size,at;char path[256];FILE*file;
   if(argc>1)CHECK(strlen(argv[1])<180);
   for(pixel=0;pixel<2;++pixel)for(which=0;which<8;++which) {
      count=scenario(code,pixel,which);memcpy(expected,inputs,sizeof(expected));
      if(which==6)for(at=0;at<4;++at)expected[at]+=2.0f;
      CHECK(xe_forward_interpret(code,pixel,inputs,out));CHECK(!memcmp(out,expected,sizeof(out)));
      size=xe_forward_spirv(code,count,pixel,binary,65536,&labels,&bools);CHECK(size>=5);
#ifndef XE_FLOW_REFERENCE
      CHECK(labels==(unsigned)(which==3 || which==4 || which==6 || which==7));CHECK(bools==0);
#endif
      at=5;switches=0;while(at<size){unsigned n=binary[at]>>16;CHECK(n && n<=size-at);switches+=(binary[at]&65535)==251;at+=n;}
      CHECK(at==size);
      (void)switches;
#ifndef XE_FLOW_REFERENCE
      CHECK(switches==(unsigned)(which==3 || which==4 || which==6 || which==7));
#endif
      if(argc>1){sprintf(path,"%s/stage%u-case%u.spv",argv[1],pixel,which);file=fopen(path,"wb");CHECK(file && fwrite(binary,4,size,file)==size && !fclose(file));}
   }
   {
      xe_spv_plan plan;xe_spv_binary snapshots[3];uint32_t ids[3];unsigned phase,found,decorations,names,blocks;
      memset(&plan,0,sizeof(plan));memset(snapshots,0,sizeof(snapshots));
      for(phase=0;phase<3;++phase) {
         CHECK(xe_forward_builder(phase,&plan,&snapshots[phase],ids));
         at=5;found=decorations=names=blocks=0;
         while(at<snapshots[phase].count) {
            const uint32_t* p=snapshots[phase].words+at;unsigned n=p[0]>>16,op=p[0]&65535;
            CHECK(n && n<=snapshots[phase].count-at);
            if(op==245 && n>=3 && p[2]==ids[2]){++found;CHECK(n==(phase==1?7u:5u));}
            decorations+=op==71 && n>=2 && p[1]==ids[1];
            names+=op==5 && n>=2 && p[1]==ids[1];blocks+=op==248 && n==2 && p[1]==ids[0];
            at+=n;
         }
         CHECK(found==1 && decorations==(unsigned)(phase==1) && names==(unsigned)(phase==1) && blocks==(unsigned)(phase==1));
         if(argc>1){sprintf(path,"%s/orphan-phase%u.spv",argv[1],phase);file=fopen(path,"wb");CHECK(file && fwrite(snapshots[phase].words,4,snapshots[phase].count,file)==snapshots[phase].count && !fclose(file));}
      }
      CHECK(snapshots[0].count==snapshots[2].count && !memcmp(snapshots[0].words,snapshots[2].words,snapshots[0].count*4));
      for(phase=0;phase<3;++phase) {
         xe_spv_binary_close(&snapshots[phase]);
      }
      xe_spv_plan_close(&plan);
      puts("PASS: orphan phi/name/decoration filtering, live pair preservation, exact/legacy dump identity and borrowed plan reuse across owners");
   }
   puts("PASS: actual VS/PS next-jump labels/unused bool elimination, shared backward labels, forward edges and call-to-next label retention; CPU control-flow semantics");
   return 0;
}
