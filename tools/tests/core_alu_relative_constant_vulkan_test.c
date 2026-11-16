/* Loop-relative constants, independent literal matrix plus native oracle. C89. */
#include "alu_continuation_address_fixture.h"
#ifndef XE_ALU_RESUME_RELATIVE_CONSTANTS
#error Loop-relative constant capability is required
#endif
static const unsigned constant_expected[80][4]={
 {42,43,44,45},
 {54,55,56,57},
 {42,43,44,45},
 {54,55,56,57},
 {42,43,44,45},
 {54,55,56,57},
 {42,43,44,45},
 {54,55,56,57},
 {42,44,46,48},
 {54,56,58,60},
 {54,56,58,60},
 {66,68,70,72},
 {43,44,45,46},
 {55,56,57,58},
 {43,44,45,46},
 {55,56,57,58},
 {46,54,64,76},
 {106,126,148,172},
 {70,90,112,136},
 {274,306,340,376},
 {46,48,50,52},
 {58,60,62,64},
 {58,60,62,64},
 {70,72,74,76},
 {43,46,49,52},
 {67,70,73,76},
 {55,58,61,64},
 {79,82,85,88},
 {43,44,45,46},
 {55,56,57,58},
 {43,44,45,46},
 {55,56,57,58},
 {45,45,45,45},
 {47,47,47,47},
 {45,45,45,45},
 {47,47,47,47},
 {57,57,57,57},
 {59,59,59,59},
 {57,57,57,57},
 {59,59,59,59},
 {45,45,45,45},
 {47,47,47,47},
 {45,45,45,45},
 {47,47,47,47},
 {57,57,57,57},
 {59,59,59,59},
 {57,57,57,57},
 {59,59,59,59},
 {45,45,45,45},
 {47,47,47,47},
 {45,45,45,45},
 {47,47,47,47},
 {57,57,57,57},
 {59,59,59,59},
 {57,57,57,57},
 {59,59,59,59},
 {45,45,45,45},
 {47,47,47,47},
 {57,57,57,57},
 {59,59,59,59},
 {45,45,45,45},
 {47,47,47,47},
 {57,57,57,57},
 {59,59,59,59},
 {85,85,85,85},
 {85,85,85,85},
 {109,109,109,109},
 {109,109,109,109},
 {85,85,85,85},
 {85,85,85,85},
 {109,109,109,109},
 {109,109,109,109},
 {85,85,85,85},
 {85,85,85,85},
 {109,109,109,109},
 {109,109,109,109},
 {85,85,85,85},
 {109,109,109,109},
 {85,85,85,85},
 {109,109,109,109}
};
typedef struct constant_boundary {
 int operand,start,count,step,base,size,bank,expected_slot;
} constant_boundary;
/* Explicit CPU-policy goldens, not computed by the tested decoder. The initial
 * unqualified fixture used raw257/260 on cases101/126; the documented operand
 * view instead clamps to256. Retain the raw progress in continuation state.
 * Native GPU specializer agreement beyond the range remains unverified. */
static const constant_boundary boundaries[32]={
 {0,0,1,0,0,511,512,0},{0,1,1,0,0,511,512,1},
 {1,0,1,0,0,511,512,1},{0,255,2,1,0,511,512,256},
 {255,255,2,1,0,511,512,511},{255,255,3,1,0,511,512,511},
 {0,0,2,-1,0,511,512,-1},{1,0,2,-1,0,511,512,0},
 {4,0,3,-2,0,511,512,0},{0,1,2,-2,0,511,512,-1},
 {0,1,1,0,257,255,512,258},{0,0,1,0,511,0,512,511},
 {0,1,1,0,511,511,512,-1},{0,0,1,0,512,511,512,-1},
 {10,1,1,0,0,511,0,-1},{0,3,1,0,0,511,4,3},
 {0,4,1,0,0,511,4,-1},{0,0,1,0,1,0,512,1},
 {0,1,1,0,1,0,512,-1},{255,1,2,-1,511,0,512,-1},
 {10,1,1,0,500,11,512,511},{10,2,1,0,500,11,512,-1},
 {0,3,1,0,0,3,512,3},{0,4,1,0,0,3,512,-1},
 {255,255,1,0,0,511,511,510},{255,255,2,1,0,511,511,-1},
 {255,0,1,0,256,255,512,511},{255,1,1,0,256,255,512,-1},
 {255,0,2,-1,0,511,512,254},{0,0,1,0,257,0,512,257},
 {0,255,6,1,0,511,512,256},{5,0,6,-1,0,511,512,0}
};
static unsigned constant_status(unsigned which)
{return which>=80 && which<=83?3u:1u;}
static uint32_t constant_word(unsigned word)
{float value=(float)(word+1);uint32_t bits;memcpy(&bits,&value,4);return bits;}
static void constant_window(uint32_t *d,unsigned which)
{
 d[5]=512;d[6]=0;d[7]=511;
 if(which>=96){const constant_boundary *c=&boundaries[which-96];d[5]=(uint32_t)c->bank;d[6]=(uint32_t)c->base;d[7]=(uint32_t)c->size;}
}
static void constant_context(uint32_t *bank,unsigned which)
{
 memset(bank,0,160);bank[8]=loop_constant(1,3,0);
 if(which>=96){const constant_boundary *c=&boundaries[which-96];bank[8]=loop_constant((unsigned)c->count,(unsigned)c->start,c->step);}
}
static void constant_program(uint32_t *p,unsigned which)
{
 unsigned family=0,prefix=0,flags=0,a=10,b=0,c=0,temps=6,vm=15,sm=0,sop=50,vop=0,a0=0,third_temp=0;
 memset(p,0,84);
 control_cf(p,0,3,0x7000);control_cf(p,1,4|(1u<<12),0x1000);
 control_cf(p,2,1,0x8000);control_cf(p,3,6|(1u<<12),0x2000);
 if(which<16){family=which/4;flags=which%4;
  if(family==1){a=0;b=10;temps=5;}
  if(family==2){a=0;b=10;temps=4;}
  if(family==3){a=0;b=1;c=10;temps=3;vop=11;}
 }else if(which<32){unsigned n=which-16;prefix=n/4;flags=n%4;a=0;b=1;c=10;temps=prefix;vop=11;
 }else if(which<64){unsigned n=which-32;prefix=n/8;flags=(n/2)%4;third_temp=n%2;a=0;b=1;c=10;temps=prefix|(third_temp<<2);vm=0;sm=15;sop=44;
 }else if(which<80){unsigned n=which-64;prefix=n/4;flags=n%4;a=0;b=1;c=10;temps=prefix;vm=0;sm=15;sop=0;
 }else if(which<=83){flags=3;a0=1;
  if(which==81){a=0;b=10;temps=5;}
  if(which==82){a=0;b=1;c=10;temps=3;vop=11;}
  if(which==83){a=0;b=1;c=10;temps=3;vm=0;sm=15;sop=44;}
 }else if(which==84){a=0;b=1;temps=7;flags=3;a0=1;
 }else if(which==85 || which==86){vm=which==85?0:15;flags=3;a0=1;
 }else if(which==87){flags=2;a0=1;
 }else if(which==88){vm=0;sm=15;flags=3;a0=1;
 }else if(which==89){a=0;b=1;c=10;temps=3;vm=0;sm=15;sop=0;flags=2;a0=1;
 }else if(which==90){a=0;b=1;c=10;temps=3;vop=11;flags=2;a0=1;
 }else if(which==91){a=0;b=10;temps=4;a0=1;
 }else if(which==92 || which==93){flags=1;
 }else if(which==94){a=0;b=1;c=10;temps=7;vm=0;sm=15;sop=44;
 }else if(which==95){a=0;b=1;temps=7;vm=sm=15;flags=3;a0=1;sop=50;control_cf(p,1,4|(1u<<12),0x2000);
 }else if(which>=96){a=(unsigned)boundaries[which-96].operand;flags=1;}
 alu(p+12,63|(63u<<8),vm,sm,sop,vop,a,b,c,temps);
 p[13]|=((flags&1)<<31)|((flags&2)<<29)|(a0<<29);
 if(which==86)p[13]|=(1u<<28)|(1u<<27);
 if(which==92){p[12]|=1u<<7;p[13]|=1u<<26;}
 if(which==93)p[13]|=0x55u<<16;
 if(which==94)p[13]|=1u<<24;
 if(which==95)p[12]=(p[12]&~63u)|62u|(1u<<15);
 alu(p+18,62,15,0,50,2,63,63,0,3);p[18]|=1u<<15;
}
static uint32_t constant_golden(unsigned which,unsigned lane)
{
 float value=0;uint32_t bits;
 if(which<80)value=(float)constant_expected[which][lane];
 else if(which==84)value=3;
 else if(which==85 || which==86)value=64;
 else if(which==87)value=(float)(42+lane);
 else if(which==88)value=0;
 else if(which==89)value=85;
 else if(which==90)value=(float)(43+lane);
 else if(which==91)value=(float)(42+lane*2);
 else if(which==92)value=(float)(-52-(int)lane);
 else if(which==93){static const unsigned rotated[4]={55,56,57,54};value=(float)rotated[lane];}
 else if(which==94)value=-47;
 else if(which==95)value=1;
 else if(which>=96){int slot=boundaries[which-96].expected_slot;value=slot<0?1.0f:(float)(slot*4+(int)lane+2);}
 memcpy(&bits,&value,4);return bits;
}
int main(void)
{
 static const address_fixture test={"relative constants","RELATIVE_CONSTANT_DEVICE",128,496,492,4,
  constant_program,constant_context,constant_status,constant_golden,constant_word,constant_window};
 return address_fixture_run(&test);
}
