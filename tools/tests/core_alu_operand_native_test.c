/* Actual CPU reference operand modifiers, independent packed goldens. C89. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
int xe_alu_operand_reference(const uint32_t*,const uint32_t*,const uint32_t*,unsigned,uint32_t*);
int main(void){uint32_t code[6],temporary[8],constants[12],output[4],wanted;
 unsigned pixel,operation,absolute,negate,i;
 for(i=0;i<8;++i)temporary[i]=0xc0e00000u;
 for(i=0;i<4;++i){constants[i]=0xbf800000u;constants[i+4]=0xc0000000u;constants[i+8]=0xc0800000u;}
 for(pixel=0;pixel<2;++pixel)for(operation=0;operation<3;++operation)
 for(absolute=0;absolute<2;++absolute)for(negate=0;negate<2;++negate){
  memset(code,0,sizeof(code));code[0]=0x1001;code[1]=0x2000;
  code[3]=62u|(1u<<15)|(absolute<<7);
  code[5]=2u|(1u<<8);
  if(!operation){code[3]|=(15u<<16)|(50u<<26);code[4]=negate?((1u<<25)|(1u<<26)):0;wanted=0x40400000u;}
  else{code[3]|=(15u<<20)|((operation==1?0u:44u)<<26);code[4]=negate<<24;wanted=operation==1?0x41000000u:0x41300000u;}
  wanted|=((absolute?0u:1u)^negate)<<31;
  if(!xe_alu_operand_reference(code,temporary,constants,pixel,output))return 2;
  for(i=0;i<4;++i)if(output[i]!=wanted){fprintf(stderr,"operand stage %u operation %u abs %u negate %u lane %u: %08x != %08x\n",pixel,operation,absolute,negate,i,output[i],wanted);return 1;}
 }
 puts("PASS: 24 actual VS/PS vector, single-source scalar and constant/register scalar absolute+negate cases");return 0;}
