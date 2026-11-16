/* Relative write corpus; shared driver borrows fixed storage. MSVC C89. */
#include "alu_continuation_address_fixture.h"
#ifndef XE_ALU_RESUME_RELATIVE_TEMP_WRITE
#error Relative-write capability is required for this fixture
#endif
static unsigned relative_status(unsigned which)
{return which==38 || which==39 || which==42?3u:1u;}
static unsigned relative_target(unsigned which)
{
 if(which==34 || which==35)return 63;
 if(which==36 || which==37 || which==40 || which==41 || which==47)return 0;
 if(which==43 || which==44)return 2;
 return 3;
}
static void relative_program(uint32_t *p,unsigned which)
{
 unsigned vm=0,sm=0,vd=2,sd=2,vr=0,sr=0,slot=4;uint32_t slots=0;
 memset(p,0,84);
 if(which<16){vm=which;vr=1;}
 else if(which<32){sm=which-16;sr=1;vd=63;vr=1;}
 else if(which==32){vm=5;sm=10;vr=sr=1;}
 else if(which==33){vm=sm=15;vr=sr=1;}
 else if(which==34){vm=15;vd=63;vr=1;}
 else if(which==35){sm=15;sd=0;sr=1;}
 else if(which==36){vm=15;vd=1;vr=1;}
 else if(which==37){sm=15;sd=1;sr=1;}
 else if(which==38){vm=15;vd=1;vr=1;}
 else if(which==39){sm=15;sd=63;sr=1;}
 else if(which==40 || which==41 || which==47){vd=sd=63;vr=sr=1;if(which==41)vm=sm=15;}
 else if(which==42){vm=15;vd=0;vr=1;}
 else {vd=sd=0;vr=sr=1;if(which&1)vm=15;else sm=15;slot=5;}
 if(which>=43 && which<=46){
  control_cf(p,0,5,0x7000);control_cf(p,1,4|(1u<<16)|(which>=45?1u<<13:0),0x7000);
  control_cf(p,2,slot|(1u<<12),0x1000);control_cf(p,3,2|(1u<<16),0x8000);
  control_cf(p,4,1,0x8000);control_cf(p,5,6|(1u<<12),0x2000);
 }else{
  control_cf(p,0,3,0x7000);control_cf(p,1,slot|(1u<<12),0x1000);
  control_cf(p,2,1,0x8000);control_cf(p,3,6|(1u<<12),0x2000);
 }
 alu(p+slot*3,vd|(vr<<6)|(sd<<8)|(sr<<14),vm,sm,sm?0u:50u,0,0,1,0,0);
 if(which==41)p[slot*3+1]|=(1u<<28)|(1u<<27);
 control_alu(p,6,62,relative_target(which),3,15,1,&slots);
}
static void relative_context(uint32_t *context,unsigned which)
{
 unsigned start=1,count=1;int step=0;
 memset(context,0,160);
 if(which==34 || which==36 || which==37)start=0;
 if(which==35 || which==38 || which==42)start=63;
 if(which==36 || which==37){count=2;step=-1;}
 if(which==42){count=2;step=1;}
 if(which>=43 && which<=46){
  count=2;step=1;context[9]=loop_constant(2,which>=45?99u:3u,which>=45?1:-1);
 }
 if(which==47)start=255;
 context[8]=loop_constant(count,start,step);
}
static uint32_t relative_golden(unsigned which,unsigned lane)
{
 unsigned mask;float f;uint32_t bits;
 f=(float)(relative_target(which)+1);memcpy(&bits,&f,4);
 if(which<16){mask=which;if(mask&(1u<<lane))bits=0x40400000u;}
 else if(which<32){mask=which-16;if(mask&(1u<<lane))bits=0x40000000u;}
 else if(which==32)bits=lane&1?0x40000000u:0x40400000u;
 else if(which==33 || which==35 || which==37 || which==44 || which==46)bits=0x40000000u;
 else if(which==34 || which==36 || which==43 || which==45)bits=0x40400000u;
 return bits;
}
static uint32_t relative_constant_word(unsigned word)
{
 static const uint32_t first[4]={0x3f800000u,0x40000000u,0x40800000u,0};
 return word<16?first[word/4]:0;
}
static void relative_window(uint32_t *descriptor,unsigned which)
{(void)which;descriptor[5]=4;descriptor[6]=0;descriptor[7]=3;}
int main(void)
{
 static const address_fixture test={"relative writes","RELATIVE_WRITE_DEVICE",48,180,180,0,
  relative_program,relative_context,relative_status,relative_golden,relative_constant_word,relative_window};
 return address_fixture_run(&test);
}
