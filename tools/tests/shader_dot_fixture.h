/* Ordered dot products: literal goldens, no host float reduction. MSVC C89. */
#ifdef XE_ALU_CONTROL_DOT
#ifdef XE_VERTEX_DOT_ZERO_REFERENCE
#define XE_DOT_CASES 78
#else
#define XE_DOT_CASES 60
#endif
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
#ifdef XE_VERTEX_DOT_ZERO_REFERENCE
 if(which>=72)return which<74?0x42200000u:0x41a00000u;
 if(which>=60)return which<66?0x80000000u:0;
#endif
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
#ifdef XE_VERTEX_DOT_ZERO_REFERENCE
 if(which>=60){
  opcode=15+((which-60)/2)%3;
  if(which>=72){
   control_cf(p,0,3,0x7000);control_cf(p,1,4|(1u<<12),0x1000);
   control_cf(p,2,1,0x8000);control_cf(p,3,6,0x2000);
  }else control_cf(p,0,4|(1u<<12),0x2000);
  alu(p+12,62,15,0,50,opcode,0,1,which>=72?127u:2u,7);p[12]|=1u<<15;return;
 }
#endif
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
 /* r[aL+63] encodes a relative source; its use is tested with aL=255 separately. */
}
static void dot_context(uint32_t *bank,unsigned which) {
 memset(bank,0,160);
#ifdef XE_VERTEX_DOT_ZERO_REFERENCE
 if(which>=72)bank[8]=loop_constant(1,255,0);
#else
 (void)which;
#endif
}
static unsigned dot_status(unsigned which) {
#ifdef XE_VERTEX_DOT_ZERO_REFERENCE
 if(which>=76)return 3;
#endif
 (void)which;return 1;
}
static void dot_temporary(uint32_t *temporary,unsigned which) {
#ifdef XE_VERTEX_DOT_ZERO_REFERENCE
 if(which>=60 && which<72){
  unsigned lane;memset(temporary,0,256*4);
  for(lane=0;lane<4;++lane){
   temporary[lane]=which<66?0x00800000u:0x80000000u;
   temporary[4+lane]=which<66?0xbf000000u:0x7f800000u;
   temporary[8+lane]=0x80000000u;
  }
  return;
 }
#endif
 memset(temporary,0,256*4);memcpy(temporary,dot_inputs[which<48?which/6:0],12*4);
}
#endif
