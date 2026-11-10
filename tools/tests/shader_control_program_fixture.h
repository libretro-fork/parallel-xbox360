/* Redistributable packed CF data; no host-side instruction normalization. C89. */
static void control_cf(uint32_t *p,unsigned at,uint32_t a,uint32_t b)
{
 unsigned word=(at/2)*3;
 if(at&1){p[word+1]|=a<<16;p[word+2]=(a>>16)|(b<<16);}
 else {p[word]=a;p[word+1]=b;}
}
static void control_alu(uint32_t *p,unsigned slot,unsigned dest,unsigned source,
 unsigned addend,unsigned mask,unsigned temporary,uint32_t *slots)
{
 alu(p+slot*3,dest,mask,0,50,0,source,addend,3,temporary);
 if(dest==62)p[slot*3]|=1u<<15;
 *slots|=1u<<slot;
}
static void program(uint32_t *p,unsigned group)
{
 unsigned kind=group%32,i;uint32_t slots=0;
 memset(p,0,21*4);
 /* Default: split dependent execution, then a final complete position. */
 if(kind==0 || (kind>=16 && kind<=21)){
  control_cf(p,0,5|(1u<<12),0x1000);control_cf(p,1,6|(1u<<12),0x2000);
  control_alu(p,5,0,0,1,15,0,&slots);control_alu(p,6,62,0,3,15,1,&slots);
 }else if(kind==1){
  control_cf(p,0,4|(1u<<12),0x1000);control_cf(p,1,4|(1u<<13),0x9000);
  control_cf(p,2,6|(1u<<12),0x2000);control_cf(p,4,5|(1u<<12),0x1000);control_cf(p,5,0,0xa000);
  control_alu(p,4,0,0,1,15,0,&slots);control_alu(p,5,0,0,1,15,1,&slots);
  control_alu(p,6,62,0,3,15,1,&slots);
 }else if(kind==2){
  for(i=0;i<4;++i){control_cf(p,i*2,(i*2+2)|(1u<<13),0x9000);
   control_cf(p,i*2+1,i?0:6,i?0xa000:0x2000);}
  control_cf(p,8,6|(1u<<12),0x1000);control_cf(p,9,0,0xa000);
  control_alu(p,6,62,0,1,15,0,&slots);
 }else if(kind==3 || kind==4){
  control_cf(p,0,(kind==3?2u:4u)|(1u<<13),0xb000);control_cf(p,1,0,0xc000);
  control_cf(p,2,6|(1u<<12),0x2000);if(kind==4)control_cf(p,4,2|(1u<<13),0xb000);
  control_alu(p,6,62,0,1,15,0,&slots);
 }else if(kind==5 || kind==6){
  control_cf(p,0,kind==5?0u:1u|(1u<<13),kind==5?0xa000:0x9000);
  control_cf(p,1,6|(1u<<12),kind==5?0x2000:0x1000);
  if(kind==6){control_cf(p,2,0,0xa000);control_cf(p,3,6,0x2000);}
  control_alu(p,6,62,0,1,15,0,&slots);
 }else if(kind==7){
  control_cf(p,0,1|(3u<<12),0x1000);control_cf(p,1,4|(3u<<12),0x2000);
  for(i=0;i<6;++i)control_alu(p,i+1,i==5?62:i,i?i-1:0,1,15,i?1:0,&slots);
 }else if(kind==8){
  control_cf(p,0,5|(1u<<12),0x1000);control_cf(p,1,6|(1u<<12),0x2000);
  control_alu(p,5,62,0,1,5,0,&slots);control_alu(p,6,62,0,1,10,0,&slots);
 }else if(kind==9 || kind==10 || kind==13){
  control_cf(p,0,6,0x1000);control_cf(p,1,6|(1u<<12),0x2000);
  control_alu(p,6,62,0,3,15,0,&slots);
 }else if(kind==11){
  control_cf(p,0,2|(1u<<13),0x9000);control_cf(p,1,0,0xc000);
  control_cf(p,2,6|(1u<<12),0x2000);control_alu(p,6,62,0,1,15,0,&slots);
 }else if(kind==12 || kind==29){
  control_cf(p,0,5|(1u<<12),0x1000);control_cf(p,1,kind==12?0u:6|(1u<<12),kind==12?0x7000:0x2000);
  control_alu(p,5,62,0,1,15,0,&slots);
  if(kind==29){control_alu(p,6,62,0,1,15,0,&slots);p[20]|=31u<<24;}
 }else if(kind==14 || kind==24){control_cf(p,0,1u<<13,kind==14?0xb000:0x9000);
 }else if(kind==15){control_cf(p,0,6,0x2000);
 }else if(kind==22 || kind==23){control_cf(p,0,2,kind==22?0xb000:0x9000);
 }else if(kind==25){control_cf(p,0,0,0xc000);
 }else if(kind==26){control_cf(p,0,8191|(1u<<13),0xb000);
 }else if(kind==27){control_cf(p,0,4095|(1u<<12),0x2000);
 }else if(kind==28){control_cf(p,0,6|(1u<<12)|(1u<<16),0x2000);
 }else if(kind==30){control_cf(p,0,7u<<12,0x2000);
 }else {control_cf(p,0,6|(1u<<12),0x3000);}
 if(group>=32 && kind<14)for(i=0;i<7;++i)if(slots&(1u<<i)){
  p[i*3]|=((group>>5)&1)<<7;
  p[i*3+1]|=(group*0x71b359u)&0xffffffu;p[i*3+1]|=((group>>5)&7)<<24;
 }
}
static unsigned expected_status(unsigned kind)
{
 if(kind==14)return 5;
 if(kind==15)return 4;
 if((kind>=16 && kind<=21) || kind==26 || kind==27)return 2;
 if(kind==12 || kind>=22)return 3;
 return 1;
}
static int control_golden(unsigned group,const uint32_t *out)
{
 static const uint32_t repeated[12]={0x40400000,0x40a00000,0x40400000,0x40400000,
  0x40400000,0x40400000,0x40400000,0x41500000,0x40400000,0x3f800000,0,0x40400000};
 static const uint32_t window[4]={0x3f800000,0xc0000000,0x40400000,0xc0800000};
 unsigned lane;
 if(group<12)for(lane=0;lane<4;++lane)if(out[lane]!=repeated[group])return 0;
 if(group==13)for(lane=0;lane<4;++lane)if(out[lane]!=window[lane])return 0;
 if(group==6 && out[5]!=2)return 0;
 if(group==8 && out[5]!=2)return 0;
 return 1;
}
