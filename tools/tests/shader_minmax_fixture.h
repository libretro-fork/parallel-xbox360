/* Ordered MIN/MAX selections with independent binary32 goldens. MSVC C89. */
#ifdef XE_ALU_CONTROL_MINMAX
#define XE_MINMAX_CASES 5136
static uint32_t minmax_bits(unsigned at) {
 static const uint32_t bits[16]={0,0x80000000u,1,0x80000001u,0x007fffffu,0x807fffffu,
  0x3f800000u,0xbf800000u,0x00800000u,0x80800000u,0x7f800000u,0xff800000u,
  0x7fc12345u,0xffc12345u,0x7f812345u,0x7f7fffffu};
 return bits[at%16];
}
static uint32_t minmax_golden_bits(uint32_t a,uint32_t b,unsigned minimum) {
 uint32_t ma,mb;unsigned equal,less;
 if(!(a&0x7f800000u))a&=0x80000000u;
 if(!(b&0x7f800000u))b&=0x80000000u;
 ma=a&0x7fffffffu;mb=b&0x7fffffffu;
 if(ma>0x7f800000u || mb>0x7f800000u)return b;
 equal=ma==mb && (!ma || (a>>31)==(b>>31));less=0;
 if(!equal)less=(a>>31)!=(b>>31)?(a>>31):(a>>31)?ma>mb:ma<mb;
 return (minimum?less:!less)?a:b;
}
static unsigned minmax_status(unsigned which) {
 unsigned q=which/2;return q==2562 || q==2563?3:1;
}
static uint32_t minmax_golden(unsigned which,unsigned lane) {
 unsigned q=which/2,mode,minimum,pair;uint32_t a,b;
 if(q>=2560){
  static const uint32_t edge[8]={0x40400000u,0x40000000u,0,0,0x3f800000u,0x3f800000u,0x40400000u,0x40000000u};
  return edge[q-2560];
 }
 if(q<512){
  mode=q/128;minimum=(q/64)%2;pair=(q%64)*4+lane;
  a=minmax_bits(pair/16);b=minmax_bits(pair);
  if(mode==1 && (lane&1))return 0;
  if(mode==3){a&=0x7fffffffu;b=minmax_bits((q%64)*4+((lane+1)&3))^0x80000000u;}
 }else{
  q-=512;mode=q/512;minimum=(q/256)%2;pair=q%256;
  a=minmax_bits(pair/16);b=minmax_bits(pair);
  if(mode==3){a=(a&0x7fffffffu)|0x80000000u;b=(b&0x7fffffffu)|0x80000000u;}
 }
 return minmax_golden_bits(a,b,minimum);
}
static void minmax_temporary(uint32_t *temporary,unsigned which) {
 unsigned q=which/2,lane,pair,selected;memset(temporary,0,256*4);
 if(q>=2560){for(lane=0;lane<4;++lane){temporary[lane]=0x40000000u;temporary[4+lane]=0x40400000u;}
  temporary[11]=0x40000000u;temporary[10]=0x40400000u;temporary[8]=0x42c80000u;return;}
 if(q<512){for(lane=0;lane<4;++lane){pair=(q%64)*4+lane;
  temporary[lane]=minmax_bits(pair/16);temporary[4+lane]=minmax_bits(pair);}return;}
 q-=512;pair=q%256;for(lane=0;lane<4;++lane)temporary[8+lane]=0x40400000u;
 if(q/512==3){selected=((pair/16)+(pair%16))&3;
  temporary[8+selected]=minmax_bits(pair/16);temporary[8+((selected+1)&3)]=minmax_bits(pair);
 }else{temporary[11]=minmax_bits(pair/16);temporary[8]=minmax_bits(pair);}
}
static void minmax_context(uint32_t *bank,unsigned which) {
 memset(bank,0,160);if(which/2>=2560)bank[8]=loop_constant(1,255,0);
}
static void minmax_program(uint32_t *p,unsigned which) {
 unsigned q=which/2,mode,minimum,selected,edge;memset(p,0,21*4);
 if(q>=2560){
  edge=q-2560;control_cf(p,0,3,0x7000);control_cf(p,1,4|(1u<<12),0x1000);
  control_cf(p,2,1,0x8000);control_cf(p,3,6,0x2000);
  if(edge<2 || edge==4 || edge==5){alu(p+12,62,15,edge>=4?15u:0u,50,2+(edge&1),edge>=4?127u:0u,edge>=4?127u:1u,127,7);p[12]|=1u<<15;}
  else if(edge<4)alu(p+12,0,0,0,5+(edge&1),0,127,127,127,7);
  else{alu(p+12,62,0,15,5+(edge&1),11,127,127,2,7);p[12]|=1u<<15;}
  return;
 }
 if(q<512){
  mode=q/128;minimum=(q/64)%2;
  control_cf(p,0,4|((mode==2?2u:1u)<<12),0x2000);
  alu(p+12,mode==2?4u:62u,mode==1?5u:15u,mode==1?10u:0u,50,2+minimum,0,1,127,7);
  if(mode!=2)p[12]|=1u<<15;
  else{alu(p+15,62,15,0,50,2,4,4,127,7);p[15]|=1u<<15;}
  if(mode==3){p[14]|=1u<<23;p[13]|=(1u<<25)|(0x55u<<8);}
  return;
 }
 q-=512;mode=q/512;minimum=(q/256)%2;
 control_cf(p,0,4|((mode==1?2u:1u)<<12),0x2000);
 alu(p+12,mode==1?0u:62u,0,mode==1?0u:15u,5+minimum,mode==3?11u:0u,127,127,2,7);
 if(mode!=1)p[12]|=1u<<15;
 if(mode==1){alu(p+15,62,0,15,50,0,127,127,127,7);p[15]|=1u<<15;}
 if(mode==2){
  control_cf(p,0,4|(2u<<12),0x1000);control_cf(p,1,6|(1u<<12),0x6000|(1u<<10));control_cf(p,2,3|(1u<<12),0x2000);
  alu(p+12,0,0,0,27,0,127,127,3,3);
  alu(p+15,0,0,0,5+minimum,0,127,127,2,7);p[16]=(1u<<28)|(1u<<27);
  alu(p+18,62,0,15,50,0,127,127,127,7);p[18]|=1u<<15;
  alu(p+9,62,15,0,50,0,1,3,127,4);p[9]|=1u<<15;
 }
 if(mode==3){selected=(((q%256)/16)+(q%16))&3;
  p[14]|=1u<<7;p[13]=(1u<<24)|(((selected+1)&3)<<6)|(((selected+3)&3)<<4)|((selected+2)&3);
 }
}
#endif
