/* Same production matcher, independent exact-key reference. MSVC C89. */
#include "libretro/core_shader_operand_lend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"operand lend line %u\n",(unsigned)__LINE__); exit(1); } } while (0)
struct operand_fixture {
   xe_shader_operand_key key;
   uint32_t modifiers, swizzle;
   unsigned valid;
};
static unsigned reads;
static int read_key(const void *opaque, unsigned index, xe_shader_operand_key *out)
{
   const struct operand_fixture *operands;
   operands = (const struct operand_fixture *)opaque;
   CHECK(index < 3);
   ++reads;
   *out = operands[index].key;
   return operands[index].valid != 0;
}
int main(void)
{
   struct operand_fixture operands[3];
   unsigned a,b,c,index,wanted,i,round;
   uint32_t state;
   memset(operands,0,sizeof(operands));
   for(i=0;i<3;++i) operands[i].valid=1;
   CHECK(xe_shader_operand_lend_index(operands,0,read_key)==0 && !reads);
   CHECK(xe_shader_operand_lend_index(NULL,1,read_key)==1);
   CHECK(xe_shader_operand_lend_index(operands,1,NULL)==1);
   CHECK(xe_shader_operand_lend_index(operands,3,read_key)==3 && !reads);
   CHECK(xe_shader_operand_lend_index(operands,~0u,read_key)==~0u && !reads);
   for(a=0;a<2;++a) for(b=0;b<3;++b) for(c=0;c<256;++c) {
      operands[0].key.source=a;operands[0].key.addressing=b;operands[0].key.index=c;
      operands[1].key=operands[0].key;operands[2].key=operands[0].key;
      operands[1].modifiers=~0u;operands[1].swizzle=0x3210;
      CHECK(xe_shader_operand_lend_index(operands,1,read_key)==0);
      CHECK(xe_shader_operand_lend_index(operands,2,read_key)==0);
      operands[0].key.source=a^1;
      CHECK(xe_shader_operand_lend_index(operands,1,read_key)==1);
      CHECK(xe_shader_operand_lend_index(operands,2,read_key)==1);
      operands[1].key.addressing=(b+1)%3;
      CHECK(xe_shader_operand_lend_index(operands,2,read_key)==2);
      operands[1].key=operands[2].key;operands[1].key.index^=UINT32_C(0x80000000);
      CHECK(xe_shader_operand_lend_index(operands,2,read_key)==2);
   }
   state=123456789;
   for(round=0;round<65536;++round) {
      for(i=0;i<3;++i) {
         state=state*1664525u+1013904223u;
         operands[i].valid=(state>>20)&1;
         operands[i].key.source=(state>>2)&1;
         operands[i].key.addressing=(state>>3)%3;
         operands[i].key.index=(state>>4)&3;
      }
      for(index=0;index<3;++index) {
         wanted=index;
         if(operands[index].valid) for(i=0;i<index;++i) {
            if(operands[i].valid && operands[i].key.source==operands[index].key.source &&
                  operands[i].key.index==operands[index].key.index &&
                  operands[i].key.addressing==operands[index].key.addressing) {
               wanted=i;break;
            }
         }
         reads=0;
         CHECK(xe_shader_operand_lend_index(operands,index,read_key)==wanted);
         CHECK(reads<=3);
      }
   }
   puts("PASS: instruction-local raw storage lends, exact keys, modifiers, invalid reads and65536 randomized plans");
   return 0;
}
