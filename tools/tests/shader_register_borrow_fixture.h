/* Immutable register views, zero tails and copy-on-write masks. MSVC C89. */
#define XE_REGISTER_CASES 896
static unsigned register_count(unsigned which) {
 static const unsigned counts[7]={0,1,31,32,33,63,64};
 return counts[((which/2)/64)%7];
}
static unsigned register_index(unsigned which) {
 static const unsigned indices[8]={0,1,30,31,32,33,62,63};
 return indices[((which/2)/8)%8];
}
static unsigned register_scenario(unsigned which){return (which/2)%8;}
static void register_temporary(uint32_t *temporary) {
 unsigned lane;float value;
 for(lane=0;lane<256;++lane){value=(float)(lane+4);memcpy(temporary+lane,&value,4);}
}
static uint32_t register_golden(unsigned which,unsigned lane) {
 unsigned index=register_index(which),s=register_scenario(which);
 unsigned original=index<register_count(which)?index*4+lane+4:0,value=original;
 uint32_t bits;float number;
 if(s==1 || s==4)value=2;
 if(s==2 && !(lane&1))value=1;
 if(s==3)value=lane&1?4:1;
 if(s==6)value=lane==1?(index<register_count(which)?index*8+11:0):original+1;
 number=(float)value;memcpy(&bits,&number,4);return bits;
}
static void register_program(uint32_t *p,unsigned which) {
 unsigned index=register_index(which),other=index^32u,s=register_scenario(which),slot=1;
 memset(p,0,21*4);
 if(s==1 || s==4){alu(p+slot*3,index,15,0,50,0,0,0,3,0);++slot;}
 if(s==2 || s==3){
  alu(p+slot*3,index,5,s==3?10u:0u,s==3?0u:50u,0,0,3,1,0);
  p[slot*3]|=index<<8;++slot;
 }
 if(s==4 || s==5){alu(p+slot*3,other,15,0,50,0,0,3,3,0);++slot;}
 if(s==6){alu(p+slot*3,index,15,2,0,0,index,0,index,5);p[slot*3]|=index<<8;++slot;}
 if(s==7){alu(p+slot*3,index,0,0,50,0,0,3,3,0);p[slot*3]|=index<<8;++slot;}
 alu(p+slot*3,62,15,0,50,0,index,3,3,1);p[slot*3]|=1u<<15;
 control_cf(p,0,1|(slot<<12),0x2000);
}
