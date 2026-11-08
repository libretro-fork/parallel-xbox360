/* Packed guest control flow shared by CPU, validation and GPU fixtures. */
#ifndef XE_TEST_SHADER_CALL_FIXTURE_H
#define XE_TEST_SHADER_CALL_FIXTURE_H
static void cf(uint32_t* code,unsigned index,uint32_t low,unsigned high) {
   uint32_t* p=code+(index/2)*3;
   if(index&1){p[1]|=low<<16;p[2]=(low>>16)|((uint32_t)high<<16);}
   else{p[0]=low;p[1]=high;}
}
static size_t nested_scenario(uint32_t code[51],unsigned pixel,unsigned mode) {
   unsigned i,low=12,high=0x9000;
   memset(code,0,51*sizeof(*code));
   cf(code,0,0x2004,0x9000);cf(code,1,0x100f,0x1000);cf(code,2,0x201c,0xb000);
   for(i=4;i<=20;i+=4) {
      cf(code,i,0x100f,0x1000);
      if(i<20)cf(code,i+1,0x2000+i+4,0x9000);
      cf(code,i+2,0,0xa000);cf(code,i+3,0x201c,0xb000);
   }
   if(mode==1)high=0x9400; /* c0=false: call skipped. */
   else if(mode==2)high=0x9000; /* c0=false: call taken. */
   else if(mode==3){low|=0x4000;high=0x9400;} /* p0=false: skipped. */
   else if(mode==4)low|=0x4000; /* p0=false: taken. */
   if(mode){code[13]&=65535;code[14]=0;cf(code,9,low,high);}
   cf(code,28,0x1010,0x2000);
   code[45]=0xc80f0000;code[46]=0;code[47]=0xc0000100;
   code[48]=(pixel?0:62)|(1u<<15)|(15u<<16)|(50u<<26);
   code[49]=0;code[50]=0xc2000000;
   return 51;
}
#endif
