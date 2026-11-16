/* Packed saturation cases and independent literal words. MSVC C89. */
#ifndef XE_TEST_SHADER_SATURATION_FIXTURE_H
#define XE_TEST_SHADER_SATURATION_FIXTURE_H
static const uint32_t samples[16]={
 0xff800000u,0xc0000000u,0x80800000u,0x80000000u,
 0,0x3e800000u,0x3f000000u,0x3f7fffffu,
 0x3f800000u,0x3f800001u,0x40000000u,0x7f7fffffu,
 0x7f800000u,0x7fc01234u,0xffc01234u,1u
};
/* Independent literals after MAX(input,input) and destination saturation. */
static const uint32_t raw[16]={
 0xff800000u,0xc0000000u,0x80800000u,0x80000000u,
 0,0x3e800000u,0x3f000000u,0x3f7fffffu,
 0x3f800000u,0x3f800001u,0x40000000u,0x7f7fffffu,
 0x7f800000u,0x7fc01234u,0xffc01234u,1u
};
static const uint32_t saturated[16]={
 0,0,0,0,0,0x3e800000u,0x3f000000u,0x3f7fffffu,
 0x3f800000u,0x3f800000u,0x3f800000u,0x3f800000u,
 0x3f800000u,0,0,1u
};
static uint32_t saturation_constant(unsigned word)
{return word<64?samples[word/4]:0;}
static unsigned saturation_status(unsigned which)
{(void)which;return 1;}
static void saturation_context(uint32_t *context,unsigned which)
{(void)which;memset(context,0,160);}
static void saturation_window(uint32_t *descriptor,unsigned which)
{(void)which;descriptor[5]=16;descriptor[6]=0;descriptor[7]=15;}
static void saturation_program(uint32_t *p,unsigned which)
{
 unsigned sample,flags,vm,sm;
 memset(p,0,84);
 control_cf(p,0,2|(1u<<12),0x1000);
 control_cf(p,1,3|(1u<<12),0x2000);
 if(which<128){
  sample=which/8;flags=which&3;vm=(which&4)?0:15;sm=vm?0:15;
 }else if(which<144){
  sample=10;flags=which&3;vm=which-128;sm=15^vm;
 }else{
  sample=which-144;flags=2;vm=0;sm=15;
 }
 alu(p+6,63|(63u<<8),vm,sm,5,2,sample,sample,sample,0);
 p[6]|=flags<<24;
 if(which>=144){
  /* RETAIN_PREV must read the raw history after a clamped temporary store. */
  alu(p+9,62|(1u<<15),0,15,50,2,0,0,0,0);
 }else{
  alu(p+9,62|(1u<<15),15,0,50,2,63,63,0,3);
 }
}
static uint32_t saturation_golden(unsigned which,unsigned lane)
{
 unsigned sample,flags,vector_lane;
 if(which>=144)return raw[which-144];
 sample=which<128?which/8:10;flags=which&3;
 vector_lane=which<128?!(which&4):((which-128)&(1u<<lane))!=0;
 return flags&(vector_lane?1u:2u)?saturated[sample]:raw[sample];
}
#endif
