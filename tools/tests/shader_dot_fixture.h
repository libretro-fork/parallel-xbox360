/* Ordered dot products: literal goldens, no host float reduction. MSVC C89. */
#ifdef XE_ALU_CONTROL_DOT
#define XE_DOT_CASES 60
static const uint32_t dot_inputs[8][12]={
 {0x3f800000u,0x40000000u,0x40400000u,0x40800000u,0x40000000u,0x40400000u,0x40800000u,0x40a00000u,0x3f000000u,0,0,0},
 {0x3f800000u,0x33c00000u,0x33c00000u,0x33c00000u,0x3f800000u,0x3f800000u,0x3f800000u,0x3f800000u,0,0,0,0},
 {0x00800000u,0x00800000u,0x00800000u,0x00800000u,0x3f000000u,0xbf000000u,0x3f000000u,0xbf000000u,0,0,0,0},
 {0x80000001u,0x00800000u,0,0,0x3f800000u,0x3f800000u,0,0,0x80000001u,0,0,0},
 {0x3f800000u,0xbf800000u,0x40000000u,0xc0000000u,0x3f800000u,0x3f800000u,0x3f800000u,0x3f800000u,0xbf000000u,0,0,0},
 {0x7f800000u,0,0,0,0,0x7f800000u,0,0,0,0,0,0},
 {0x7f800000u,0xff800000u,0,0,0x3f800000u,0x3f800000u,0,0,0,0,0,0},
 {0x7f7fffffu,0x7f7fffffu,0,0,0x3f800000u,0x3f800000u,0,0,0,0,0,0}
};
static uint32_t dot_golden(unsigned which,unsigned lane) {
 static const uint32_t expected[8][3]={
  {0x42200000u,0x41a00000u,0x41080000u},
  {0x3f800000u,0x3f800000u,0x3f800000u},
  {0,0,0},
  {0x00800000u,0x00800000u,0x00800000u},
  {0,0x40000000u,0xbf000000u},
  {0,0,0},
  {0x7fc00000u,0x7fc00000u,0x7fc00000u},
  {0x7f7fffffu,0x7f7fffffu,0x7f7fffffu}
 };
 if(which>=48){
  unsigned edge=(which-48)/2;
  if(edge==0)return lane&1?0:0x42200000u;
  if(edge==1)return 0x3f800000u;
  if(edge==2)return 0x42200000u;
  if(edge==3)return lane&1?0x3f000000u:0x42200000u;
  if(edge==4)return 0xc2200000u;
  return 0x41f00000u;
 }
 return expected[which/6][(which/2)%3];
}
static void dot_program(uint32_t *p,unsigned which) {
 unsigned opcode=15+(which/2)%3,edge;memset(p,0,21*4);
 if(which>=48)opcode=15;
 control_cf(p,0,4|(1u<<12),0x2000);
 alu(p+12,62,15,0,50,opcode,0,1,opcode==17?2u:127u,7);p[12]|=1u<<15;
 if(which>=48){
  edge=(which-48)/2;
  if(edge==0)p[12]=(p[12]&~(15u<<16))|(5u<<16)|(10u<<20);
  if(edge==1)p[12]|=15u<<20;
  if(edge==2){
   control_cf(p,0,4|(2u<<12),0x2000);
   alu(p+12,4,15,0,50,15,0,1,127,7);
   alu(p+15,62,15,0,50,0,4,3,127,7);p[15]|=1u<<15;
  }
  if(edge==3){alu(p+12,62,5,10,0,15,0,1,2,7);p[12]|=1u<<15;}
  if(edge==4)p[13]|=1u<<26;
  if(edge==5)p[13]|=0x77u<<16; /* Component-relative wzyx. */
 }
 /* The unused third source is deliberately out of the temporary window. */
}
static void dot_temporary(uint32_t *temporary,unsigned which) {
 memset(temporary,0,256*4);memcpy(temporary,dot_inputs[which<48?which/6:0],12*4);
}
#endif
