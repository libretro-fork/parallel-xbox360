/* Fixed exact-rational boundary literals; no runtime oracle construction. C89. */
#ifndef XE_TEST_SHADER_ROUNDING_FIXTURE_H
#define XE_TEST_SHADER_ROUNDING_FIXTURE_H
static const uint32_t rounding_samples[32]={
 0x00000000u,0x80000000u,0x00000001u,0x80000001u,0x007fffffu,0x807fffffu,0x00800000u,0x80800000u,
 0x3e800000u,0xbe800000u,0x3f000000u,0xbf000000u,0x3f7fffffu,0xbf7fffffu,0x3f800000u,0xbf800000u,
 0x3f800001u,0xbf800001u,0x3fc00000u,0xbfc00000u,0x40100000u,0xc0100000u,0x4affffffu,0xcaffffffu,
 0x4b000000u,0xcb000000u,0x7f7fffffu,0xff7fffffu,0x7f800000u,0xff800000u,0x7fc01234u,0xffc01234u
};
static const uint32_t rounding_result_0[32]={
 0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00800000u,0x3f7fffffu,
 0x3e800000u,0x3f400000u,0x3f000000u,0x3f000000u,0x3f7fffffu,0x33800000u,0x00000000u,0x00000000u,
 0x34000000u,0x3f7ffffeu,0x3f000000u,0x3f000000u,0x3e800000u,0x3f400000u,0x3f000000u,0x3f000000u,
 0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x7fc00000u,0x7fc00000u,0x7fc00000u,0x7fc00000u
};
static const uint32_t rounding_result_1[32]={
 0x00000000u,0x80000000u,0x00000000u,0x80000000u,0x00000000u,0x80000000u,0x00000000u,0x80000000u,
 0x00000000u,0x80000000u,0x00000000u,0x80000000u,0x00000000u,0x80000000u,0x3f800000u,0xbf800000u,
 0x3f800000u,0xbf800000u,0x3f800000u,0xbf800000u,0x40000000u,0xc0000000u,0x4afffffeu,0xcafffffeu,
 0x4b000000u,0xcb000000u,0x7f7fffffu,0xff7fffffu,0x7f800000u,0xff800000u,0x7fc01234u,0xffc01234u
};
static const uint32_t rounding_result_2[32]={
 0x00000000u,0x80000000u,0x00000000u,0x80000000u,0x00000000u,0x80000000u,0x00000000u,0xbf800000u,
 0x00000000u,0xbf800000u,0x00000000u,0xbf800000u,0x00000000u,0xbf800000u,0x3f800000u,0xbf800000u,
 0x3f800000u,0xc0000000u,0x3f800000u,0xc0000000u,0x40000000u,0xc0400000u,0x4afffffeu,0xcb000000u,
 0x4b000000u,0xcb000000u,0x7f7fffffu,0xff7fffffu,0x7f800000u,0xff800000u,0x7fc01234u,0xffc01234u
};
static uint32_t rounding_value(unsigned op,unsigned sample) {
 return op==0?rounding_result_0[sample]:op==1?rounding_result_1[sample]:rounding_result_2[sample];
}
#ifndef XE_ROUNDING_LITERALS_ONLY
static uint32_t rounding_constant(unsigned word) {
 static const unsigned mixed[4]={18,19,6,7};
 if(word<128)return rounding_samples[word/4];
 if(word<132)return rounding_samples[mixed[word-128]];
 return word<136?0x3f800000u:0;
}
static unsigned rounding_status(unsigned which){(void)which;return 1;}
static void rounding_context(uint32_t *context,unsigned which){(void)which;memset(context,0,160);}
static void rounding_window(uint32_t *descriptor,unsigned which){(void)which;descriptor[5]=34;descriptor[6]=0;descriptor[7]=33;}
static unsigned rounding_tail(unsigned which){return which>=240 && which<288?12:9;}
static void rounding_program(uint32_t *p,unsigned which) {
 unsigned op,sample,mask,variant,tail=rounding_tail(which);
 memset(p,0,84);
 control_cf(p,0,2|((tail==12?2u:1u)<<12),0x1000);
 control_cf(p,1,(tail/3)|(1u<<12),0x2000);
 if(which<192){
  sample=which/6;op=(which/2)%3;
  if(which&1)alu(p+6,63|(63u<<8),0,15,11+op,0,255,255,sample,0);
  else alu(p+6,63,15,0,50,8+op,sample,255,255,0);
 }else if(which<240){
  op=(which-192)/16;mask=(which-192)%16;
  alu(p+6,63|(63u<<8),mask,15^mask,11+op,8+op,32,255,32,0);
 }else if(which<288){
  op=(which-240)/16;variant=(which-240)%16;
  alu(p+6,0,15,0,50,2,18,18,0,0);
  alu(p+9,63,15,0,50,8+op,0,255,255,1|((variant&1)?2u:0)|((variant&2)?4u:0));
  if(variant&4)p[10]|=(1u<<31)|(1u<<29);
  if(variant&8)p[10]|=1u<<26;
 }else{
  op=(which-288)%3;sample=(which-288)/3;
  /* History updates even without a store; a clamp flag must not modify it. */
  alu(p+6,63|(63u<<8),0,0,11+op,0,255,255,sample,0);p[6]|=1u<<25;
  alu(p+9,62|(1u<<15),0,15,50,0,255,255,255,0);return;
 }
 alu(p+tail,62|(1u<<15),15,0,50,2,63,63,0,3);
}
static uint32_t rounding_golden(unsigned which,unsigned lane) {
 static const unsigned mixed[4]={18,19,6,7};
 unsigned op,sample,mask;
 if(which<192){sample=which/6;op=(which/2)%3;}
 else if(which<240){op=(which-192)/16;mask=(which-192)%16;sample=mixed[(mask&(1u<<lane))?lane:3];}
 else if(which<288){op=(which-240)/16;sample=((which-240)&8)?19:18;}
 else{op=(which-288)%3;sample=(which-288)/3;}
 return rounding_value(op,sample);
}
#endif
#endif
