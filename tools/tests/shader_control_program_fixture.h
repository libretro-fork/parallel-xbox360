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
 }else if(kind==22 || kind==23){control_cf(p,0,2,kind==22?0xc000:0xf000);
 }else if(kind==25){control_cf(p,0,0,0xc000);
 }else if(kind==26){control_cf(p,0,8191|(1u<<13),0xb000);
 }else if(kind==27){control_cf(p,0,4095|(1u<<12),0x2000);
 }else if(kind==28){control_cf(p,0,6|(1u<<12)|(1u<<16),0x2000);
 }else if(kind==30){control_cf(p,0,7u<<12,0x2000);
 }else {control_cf(p,0,6|(1u<<12),0xf000);}
 if(group>=32 && kind<14)for(i=0;i<7;++i)if(slots&(1u<<i)){
  p[i*3]|=((group>>5)&1)<<7;
  p[i*3+1]|=(group*0x71b359u)&0xffffffu;p[i*3+1]|=((group>>5)&7)<<24;
 }
}
#ifdef XE_ALU_CONTROL_BOOL
#define XE_BOOL_CASES 224
static unsigned bool_index(unsigned which)
{
 static const unsigned indices[8]={0,31,32,63,127,128,254,255};
 return indices[(which>>2)&7];
}
static int bool_taken(unsigned which){return (which&1)==((which>>1)&1);}
static void bool_context(uint32_t bank[40],unsigned which)
{
 unsigned index=bool_index(which);
 memset(bank,which&1?0:255,160);
 if(which&1)bank[index>>5]|=1u<<(index&31);
 else bank[index>>5]&=~(1u<<(index&31));
}
static unsigned bool_status(unsigned which)
{
 unsigned scenario=which/32;
 if(!bool_taken(which))return 1;
 return scenario==4?2:scenario>=5?3:1;
}
static uint32_t bool_golden(unsigned which)
{
 unsigned scenario=which/32;
 if(scenario==0 || scenario==2)return bool_taken(which)?0x40400000u:0x3f800000u;
 if(scenario==1 || scenario==3)return bool_taken(which)?0x40400000u:0x40000000u;
 return 0x40400000u;
}
static void bool_program(uint32_t *p,unsigned which)
{
 unsigned scenario=which/32,i;uint32_t slots=0;
 uint32_t condition=(bool_index(which)<<2)|(((which>>1)&1)<<10);
 memset(p,0,21*4);
 if(scenario==0 || scenario==2){
  control_cf(p,0,4|(1u<<12),0x1000);
  control_cf(p,1,scenario==0?5|(1u<<12):4,condition|(scenario==0?0x3000:0x9000));
  control_cf(p,2,6|(1u<<12),0x2000);
  if(scenario==2){control_cf(p,4,5|(1u<<12),0x1000);control_cf(p,5,0,0xa000);}
  control_alu(p,4,0,0,3,15,0,&slots);control_alu(p,5,0,0,1,15,1,&slots);
  control_alu(p,6,62,0,3,15,1,&slots);
 }else if(scenario==1 || scenario==3){
  control_cf(p,0,scenario==1?5|(1u<<12):2,condition|(scenario==1?0x4000:0xb000));
  control_cf(p,1,6|(1u<<12),0x2000);
  if(scenario==3)control_cf(p,2,5|(1u<<12),0x2000);
  control_alu(p,5,62,0,1,15,0,&slots);control_alu(p,6,62,0,0,15,0,&slots);
 }else if(scenario==4 || scenario==5){
  control_cf(p,0,scenario==4?8191u:5|(1u<<12),condition|(scenario==4?0xb000:0x3000));
  control_cf(p,1,6|(1u<<12),0x2000);
  control_alu(p,5,62,0,1,15,0,&slots);p[17]|=31u<<24;
  control_alu(p,6,62,0,1,15,0,&slots);
 }else{
  for(i=0;i<4;++i){control_cf(p,i*2,(i*2+2)|(1u<<13),0x9000);
   control_cf(p,i*2+1,i?0:6|(1u<<12),i?0xa000:0x2000);}
  control_cf(p,8,10,condition|0x9000);control_cf(p,9,0,0xa000);
  control_alu(p,6,62,0,1,15,0,&slots);
 }
}
#endif
#ifdef XE_ALU_CONTROL_LOOPS
#define XE_LOOP_CASES 36
static uint32_t loop_constant(unsigned count,unsigned start,int step)
{return count|(start<<8)|(((uint32_t)step&255)<<16);}
static void loop_context(uint32_t bank[40],unsigned which)
{
 unsigned scenario=which/2,i;
 memset(bank,0,160);bank[8]=loop_constant(3,1,1);
 if(scenario==1)bank[8]=loop_constant(4,6,-2);
 if(scenario==2)bank[8]=loop_constant(3,99,1);
 if(scenario==3){bank[8]=loop_constant(2,5,2);bank[9]=loop_constant(3,99,-1);}
 if(scenario==4)for(i=0;i<4;++i)bank[8+i]=loop_constant(1,i?99:5,1);
 if(scenario==5){bank[8]=loop_constant(1,5,1);bank[9]=loop_constant(2,99,1);bank[10]=loop_constant(1,99,-1);}
 if(scenario==6)bank[8]=0;
 if(scenario==7)for(i=0;i<5;++i)bank[8+i]=loop_constant(1,0,0);
 if(scenario==9)bank[8]=bank[9]=loop_constant(255,0,0);
 if(scenario==10)bank[8]=loop_constant(2,63,1);
 if(scenario==11)bank[8]=loop_constant(3,0,0);
 if(scenario==12)bank[8]=loop_constant(2,1,1);
 if(scenario==14){bank[8]=0;bank[39]=loop_constant(3,1,1);}
 if(scenario==15)bank[8]=loop_constant(255,0,0);
 if(scenario==16)bank[8]=loop_constant(3,255,0);
 if(scenario==17)bank[8]=loop_constant(2,0,-1);
}
static unsigned loop_status(unsigned which)
{
 unsigned scenario=which/2;
 return scenario==8?2:scenario==9?5:(scenario==6 || scenario==7 || scenario==10)?3:1;
}
static uint32_t loop_golden(unsigned which,unsigned lane)
{
 static const uint32_t values[18]={0x40500000,0x3e800000,0x40100000,0x40a80000,
  0x40a80000,0x40a80000,0,0,0,0,0,0x40500000,0x40100000,0x40500000,0x40500000,0x3e800000,
  0x3e800000,0x427d0000};
 return which/2==11 && (lane&1)?0x40000000u:values[which/2];
}
static unsigned loop_exports(unsigned which)
{
 static const unsigned counts[18]={3,4,3,6,1,3,0,0,0,0,0,1,2,3,3,255,3,2};
 return counts[which/2];
}
static void loop_program(uint32_t *p,unsigned which)
{
 unsigned scenario=which/2,i;uint32_t slots=0;
 memset(p,0,21*4);control_alu(p,6,62,64,3,15,1,&slots);
 /* Direct register reads ignore aL even when it is outside the register bank. */
 if(scenario>=16)control_alu(p,6,62,scenario==16?0u:63u,3,15,1,&slots);
 if(scenario<3 || scenario==6 || scenario==10 || scenario>=14){
  control_cf(p,0,3|(scenario==2?1u<<13:0),0x7000);control_cf(p,1,6|(1u<<12),0x1000);
  control_cf(p,2,1,0x8000);control_cf(p,3,6,0x2000);
  if(scenario==14){p[0]|=31u<<16;p[3]|=31u<<16;}
 }else if(scenario==3 || scenario==9){
  control_cf(p,0,5,0x7000);control_cf(p,1,4|(1u<<16)|(scenario==3?1u<<13:0),0x7000);
  control_cf(p,2,6|(1u<<12),0x1000);control_cf(p,3,2|(1u<<16),0x8000);
  control_cf(p,4,1,0x8000);control_cf(p,5,6,0x2000);
 }else if(scenario==4){
  for(i=0;i<4;++i)control_cf(p,i,(9-i)|(i<<16)|(i?1u<<13:0),0x7000);
  control_cf(p,4,6|(1u<<12),0x1000);
  for(i=0;i<4;++i)control_cf(p,5+i,(4-i)|((3-i)<<16),0x8000);
  control_cf(p,9,6,0x2000);
 }else if(scenario==5){
  control_cf(p,0,8,0x7000);control_cf(p,1,4|(1u<<13)|(1u<<16),0x7000);
  control_cf(p,2,6|(1u<<12),0x1000);control_cf(p,3,2|(1u<<16),0x8000);
  control_cf(p,4,7|(1u<<13)|(2u<<16),0x7000);control_cf(p,5,6|(1u<<12),0x1000);
  control_cf(p,6,5|(2u<<16),0x8000);control_cf(p,7,1,0x8000);control_cf(p,8,6,0x2000);
 }else if(scenario==7){for(i=0;i<5;++i)control_cf(p,i,(11-i)|(i<<16),0x7000);
 }else if(scenario==8){control_cf(p,0,1,0x8000);
 }else if(scenario==11){
  control_cf(p,0,3,0x7000);control_cf(p,1,5|(1u<<12),0x1000);control_cf(p,2,1,0x8000);
  control_cf(p,3,6|(1u<<12),0x2000);
  control_alu(p,5,0,0,0,15,1,&slots);p[15]&=0x03ffffffu;p[17]&=~255u;
  control_alu(p,6,62,0,3,5,1,&slots);p[18]|=10u<<20;
 }else if(scenario==12){
  control_cf(p,0,3,0x7000);control_cf(p,1,4|(1u<<13),0x9000);control_cf(p,2,1,0x8000);
  control_cf(p,3,6,0x2000);control_cf(p,4,6|(1u<<12),0x1000);control_cf(p,5,0,0xa000);
 }else{
  control_cf(p,0,2|(1u<<13),0x9000);control_cf(p,1,6,0x2000);control_cf(p,2,5,0x7000);
  control_cf(p,3,6|(1u<<12),0x1000);control_cf(p,4,3,0x8000);control_cf(p,5,0,0xa000);
 }
}
#endif
#ifdef XE_ALU_CONTROL_PREDICATE
#define XE_PRED_CASES 1254
/* Last two replays share one program but give adjacent invocations opposite p0. */
static unsigned pred_case(unsigned which,unsigned invocation)
{return which<1250?which:which<1252?96+(invocation?8:0)+(which&1):14*96+(which&1);}
static unsigned pred_scenario(unsigned which){return which/96;}
static unsigned pred_opcode(unsigned which){return (which%96)/24;}
static unsigned pred_input(unsigned which){return (which/2)%12;}
static uint32_t pred_bits(unsigned which)
{
 static const uint32_t values[12]={0,0x80000000u,1,0x80000001u,0x3f800000u,0xbf800000u,
  0x00800000u,0x80800000u,0x7f800000u,0xff800000u,0x7fc12345u,0xffc12345u};
 return values[pred_input(which)];
}
static unsigned pred_value(unsigned which)
{
 static const unsigned values[4][12]={
  {1,1,1,1,0,0,0,0,0,0,0,0},{0,0,0,0,1,1,1,1,1,1,1,1},
  {0,0,0,0,1,0,1,0,1,0,0,0},{1,1,1,1,1,0,1,0,1,0,0,0}};
 return pred_scenario(which)==13?0:values[pred_opcode(which)][pred_input(which)];
}
static void pred_context(uint32_t *bank,unsigned which)
{
 memset(bank,0,160);if(!pred_value(which))bank[7]=0x80000000u;
}
static void pred_temporary(uint32_t *temporary,unsigned which)
{
 unsigned lane,selected=(which/2)&3,op=pred_opcode(which),value=pred_value(which);
 uint32_t wrong=op==0?(value?0x3f800000u:0):op==1?(value?0:0x3f800000u):(value?0:0x3f800000u);
 if(op==3 && value)wrong=0xbf800000u;
 for(lane=0;lane<4;++lane){temporary[lane]=0x40000000u;temporary[8+lane]=wrong;}
 temporary[8+selected]=pred_bits(which);
}
static unsigned pred_status(unsigned which)
{
 unsigned scenario=pred_scenario(which),value=pred_value(which);
 return (scenario==7 || scenario==9) && value?3:scenario==8 && !value?2:1;
}
static uint32_t pred_golden(unsigned which,unsigned lane)
{
 unsigned scenario=pred_scenario(which),value=pred_value(which);
 if(scenario==14)return lane&1?0x40000000u:0x40400000u;
 if(scenario==0 || scenario==12)return value?0:0x3f800000u;
 if(scenario==1 || scenario==3 || scenario==5)return value?0x40400000u:0x40000000u;
 if(scenario==2 || scenario==4 || scenario==6 || scenario==10 || scenario==11)
  return value?0x40000000u:0x40400000u;
 return 0x40000000u;
}
static unsigned pred_exports(unsigned which)
{return pred_scenario(which)==14 || (pred_scenario(which)==10 && pred_value(which))?2:1;}
static void pred_program(uint32_t *p,unsigned which)
{
 unsigned scenario=pred_scenario(which),selected=(which/2)&3;uint32_t slots=0;
 memset(p,0,21*4);
 control_cf(p,0,4|(1u<<12),0x1000);control_cf(p,2,6|(1u<<12),0x2000);
 alu(p+12,0,0,0,27+pred_opcode(which),0,0,3,2,4);p[13]=((selected+1)&3)<<6;
 control_alu(p,5,0,0,1,15,0,&slots);control_alu(p,6,62,0,3,15,1,&slots);
 if(scenario==14){
  control_cf(p,0,5|(1u<<12),0x1000);control_cf(p,1,4|(1u<<12),0x1000);
  control_alu(p,5,62,0,1,5,0,&slots);control_alu(p,6,62,0,3,10,1,&slots);
 }else if(scenario==0 || scenario==12){
  if(scenario==0)control_cf(p,1,6|(1u<<12),0x2000);
  else{
   control_cf(p,1,5|(1u<<12),0x1000);
   p[15]=(p[15]&0x03ffffffu);p[17]=(p[17]&~255u);
   p[16]|=(1u<<28)|((1u-pred_value(which))<<27);
  }
  p[18]&=~(15u<<16);p[18]|=15u<<20;
 }else if(scenario==1 || scenario==2 || scenario==9 || scenario==13){
  control_cf(p,1,5|(1u<<12),(scenario==1 || scenario==13?0x5000u:0x6000u)|
   (scenario==2?0u:1u<<10));
  if(scenario==2 || scenario==9)control_alu(p,5,62,0,1,15,0,&slots);
  if(scenario==9)p[17]|=31u<<24;
  if(scenario==13){p[0]=0;p[1]&=0xffff0000u;}
 }else if(scenario==3){
  control_cf(p,1,4|(1u<<14),0x9000|(1u<<10));
  control_cf(p,4,5|(1u<<12),0x1000);control_cf(p,5,0,0xa000);
 }else if(scenario==4 || scenario==8){
  control_cf(p,1,(scenario==8?8191u:3u)|(1u<<14),0xb000);
  control_cf(p,3,5|(1u<<12),0x2000);control_alu(p,5,62,0,1,15,0,&slots);
 }else if(scenario==5 || scenario==6 || scenario==7){
  control_cf(p,1,5|(1u<<12),0x1000);p[16]|=(1u<<28)|(scenario==6?0u:1u<<27);
  if(scenario==6){
   p[15]=(p[15]&0x03ffffffu)|(27u<<26);p[17]=(p[17]&~255u)|3;
   control_cf(p,2,6|(1u<<12),0x6000|(1u<<10));control_cf(p,3,4|(1u<<12),0x2000);
  }
  if(scenario==7)p[17]|=31u<<24;
 }else{
  control_cf(p,1,5|(1u<<12),(255u<<2)|(1u<<10)|(scenario==10?0xd000u:0xe000u));
  if(scenario==11)control_alu(p,5,62,0,1,15,0,&slots);
  else{control_cf(p,2,6|(1u<<12),0x5000|(1u<<10));control_cf(p,3,6|(1u<<12),0x2000);}
 }
}
#endif
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
