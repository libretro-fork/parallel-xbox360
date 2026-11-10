/* Literal lane/bit goldens and constant-view identity. MSVC C89. */
#include "libretro/core_alu_operand.h"
#include <stdio.h>
#include <limits.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"ALU operand line %u\n",(unsigned)__LINE__);return 1;}}while(0)
static uint32_t bank[512*4];
int main(void){static const uint32_t values[]={0,0x80000000u,1,0x80000001u,0x3f800000u,0xbf800000u,0x7f800000u,0xff800000u,0x7fc12345u,0xff812345u};
 static const uint32_t literal_source[4]={0x3f800000u,0x40000000u,0x40400000u,0x40800000u};
 static const uint32_t literal_swizzle[8]={0,0x55,0xaa,0xff,0x6c,0xb1,0xc6,0x1b};
 static const unsigned literal_lanes[8][4]={
  {0,1,2,3},{1,2,3,0},{2,3,0,1},{3,0,1,2},
  {0,0,0,0},{1,1,1,1},{2,2,2,2},{3,3,3,3}};
 uint32_t source[4],wanted,bits;float value,vector[4];unsigned i,lane,swizzle,absolute,negate;
 for(i=0;i<8;++i){
  xe_alu_source_vector(vector,literal_source,literal_swizzle[i],0,0);
  for(lane=0;lane<4;++lane){
   value=xe_alu_source_value(literal_source,literal_swizzle[i],lane,0,0);
   memcpy(&bits,&value,4);CHECK(bits==literal_source[literal_lanes[i][lane]]);
   memcpy(&bits,vector+lane,4);CHECK(bits==literal_source[literal_lanes[i][lane]]);
  }
 }
 for(i=0;i<512;++i)CHECK(xe_alu_constant_view(bank,i,0,INT32_MAX,0,511)==bank+4*i);
 CHECK(xe_alu_constant_view(bank,255,0,0,255,255)==bank+4*510);
 CHECK(xe_alu_constant_view(bank,0,0,0,511,0)==bank+4*511);
 CHECK(xe_alu_constant_view(bank,1,1,-1,511,0)==bank+4*511);
 CHECK(xe_alu_constant_view(bank,0,1,1,510,1)==bank+4*511);
 CHECK(!xe_alu_constant_view(bank,0,1,-1,0,511));
 CHECK(!xe_alu_constant_view(bank,256,0,0,0,255));
 CHECK(!xe_alu_constant_view(bank,1,0,0,511,511));
 CHECK(!xe_alu_constant_view(bank,255,1,INT32_MAX,0,511));
 CHECK(!xe_alu_constant_view(bank,0,1,INT32_MIN,0,511));
 CHECK(!xe_alu_constant_view(bank,1,0,0,UINT32_MAX,UINT32_MAX));
 CHECK(!xe_alu_constant_view(NULL,0,0,0,0,0));
 for(i=0;i<sizeof(values)/sizeof(values[0]);++i){
  for(lane=0;lane<4;++lane)source[lane]=values[(i+lane)%10];
  for(swizzle=0;swizzle<256;++swizzle)for(lane=0;lane<4;++lane)
  for(absolute=0;absolute<2;++absolute)for(negate=0;negate<2;++negate){
   wanted=source[(((swizzle>>(lane*2))&3)+lane)%4];
   if((wanted&0x7fffffffu)<0x800000u)wanted&=0x80000000u;
   if(absolute)wanted&=0x7fffffffu;
   if(negate)wanted^=0x80000000u;
   value=xe_alu_source_value(source,swizzle,lane,(int)absolute,(int)negate);memcpy(&bits,&value,4);CHECK(bits==wanted);
   xe_alu_source_vector(vector,source,swizzle,(int)absolute,(int)negate);
   memcpy(&bits,vector+lane,4);CHECK(bits==wanted);
  }
 }
 CHECK(xe_alu_source_value(NULL,0,0,0,0)==0.0f);CHECK(xe_alu_source_value(source,0,4,0,0)==0.0f);
 xe_alu_source_vector(vector,NULL,0,0,0);
 for(lane=0;lane<4;++lane){memcpy(&bits,vector+lane,4);CHECK(bits==0);}
 puts("PASS: borrowed 512-constant identity/bounds/overflow and 40960 exact lane/modifier/FTZ cases");return 0;}
