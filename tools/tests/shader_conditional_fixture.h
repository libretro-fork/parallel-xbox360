/* Literal conditional predicates and raw selection payloads. MSVC C89.
 * The last four conditions separately probe the existing input-FTZ policy;
 * a disagreement there must not be confused with selected-payload loss.
 * Bits0/1/2 below mean EQ/GE/GT against zero. */
#ifndef XE_TEST_SHADER_CONDITIONAL_FIXTURE_H
#define XE_TEST_SHADER_CONDITIONAL_FIXTURE_H
static const uint32_t conditional_conditions[28]={
 0,0x80000000u,0x3f800000u,0xbf800000u,0x00800000u,0x80800000u,
 0x7f7fffffu,0xff7fffffu,0x7f800000u,0xff800000u,0x7fc01234u,0xffc01234u,
 0x7f801234u,0xff801234u,0x3f000000u,0xbf000000u,0x3f7fffffu,0xbf7fffffu,
 0x3f800001u,0xbf800001u,0x40000000u,0xc0000000u,0x00800001u,0x80800001u,
 1,0x80000001u,0x007fffffu,0x807fffffu
};
static const unsigned conditional_predicates[28]={
 3,3,6,0,6,0,6,0,6,0,0,0,0,0,6,0,6,0,6,0,6,0,6,0,3,3,3,3
};
static const uint32_t conditional_payloads[16]={
 1,0x80000001u,0x007fffffu,0x807fffffu,0,0x80000000u,0x7fc01234u,0xffc05678u,
 0x7f800000u,0xff800000u,0x3f800000u,0xbf800000u,0x00800000u,0x80800000u,0x7f7fffffu,0xff7fffffu
};
#ifndef XE_CONDITIONAL_PROGRAM_FIXTURE
static void conditional_inputs(uint32_t *out,unsigned which) {
 unsigned sample=which<72?which/3:(which-72)/3,lane;
 for(lane=0;lane<4;++lane){
  out[lane]=conditional_conditions[which<72?(sample+lane)%24:24+sample];
  out[4+lane]=conditional_payloads[(sample+lane)%16];
  out[8+lane]=conditional_payloads[(sample+lane+5)%16];
  out[12+lane]=0x3f800000u;
 }
}
#endif
static uint32_t conditional_golden(unsigned which,unsigned lane) {
 unsigned sample=which<72?which/3:(which-72)/3,condition=which<72?(sample+lane)%24:24+sample,op=which%3;
 return conditional_payloads[(sample+lane+((conditional_predicates[condition]&(1u<<op))?0:5))%16];
}
#ifdef XE_CONDITIONAL_PROGRAM_FIXTURE
static uint32_t conditional_constant(unsigned word) {
 unsigned sample=word/12,slot=(word%12)/4,lane=word%4,payload_sample;
 if(sample>=28)return word<340?0x3f800000u:0;
 if(slot==0)return conditional_conditions[sample<24?(sample+lane)%24:sample];
 /* Denormal condition rows reuse payload rows0..3, as in the native audit. */
 payload_sample=sample<24?sample:sample-24;
 return conditional_payloads[(payload_sample+lane+(slot==1?0:5))%16];
}
static void conditional_context(uint32_t *context,unsigned which){(void)which;memset(context,0,160);}
static unsigned conditional_status(unsigned which){(void)which;return 1;}
static void conditional_window(uint32_t *descriptor,unsigned which){(void)which;descriptor[5]=85;descriptor[6]=0;descriptor[7]=84;}
static void conditional_program(uint32_t *p,unsigned which) {
 unsigned sample=which<84?which/3:0,op=which<84?which%3:(which-84)/16,mask=15;
 memset(p,0,84);control_cf(p,0,2|(1u<<12),0x1000);control_cf(p,1,3|(1u<<12),0x2000);
 if(which<84)alu(p+6,63,15,0,50,12+op,sample*3,sample*3+1,sample*3+2,0);
 else if(which<132){
  mask=(which-84)%16;alu(p+6,63|(63u<<8),mask,15^mask,50,12+op,0,1,2,0);
 }else{
  unsigned variant=(which-132)%16;op=(which-132)/16;
  alu(p+6,63,0,0,50,12+op,255,255,255,variant&7);
  if(variant&8)p[7]|=(1u<<31)|(1u<<29);
 }
 alu(p+9,62|(1u<<15),15,0,50,2,63,63,0,3);
}
static uint32_t conditional_program_golden(unsigned which,unsigned lane) {
 unsigned mask,op;
 if(which<84)return conditional_golden(which,lane);
 if(which>=132)return 0x42800000u; /* Borrowed initial r63 remains64. */
 mask=(which-84)%16;op=(which-84)/16;
 return mask&(1u<<lane)?conditional_golden(op,lane):0;
}
#endif
#endif
