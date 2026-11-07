/* Actual title/hash option selection and PPC patch words. MSVC C89. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
int xe_ng2_shadow_query(uint32_t,uint64_t,char*,size_t,uint32_t*,unsigned*);
int xe_ng2_shadow_forget(void);
#define CHECK(x) do {if(!(x)){fprintf(stderr,"NG2 shadow line %u\n",(unsigned)__LINE__);return 1;}}while(0)
int main(void) {
   static const uint64_t hashes[]={UINT64_C(0xAFF931BFF00F8A0A),UINT64_C(0x4CC4201C0C4ED58B)};
   static const uint32_t initial[]={UINT32_C(0x82332248),UINT32_C(0x82332250)};
   char key[64],again[64],world[64];uint32_t words[4];unsigned enabled,i;
   for(i=0;i<2;++i) {
      CHECK(xe_ng2_shadow_query(UINT32_C(0x544307D5),hashes[i],key,sizeof(key),words,&enabled)==1);
      CHECK(enabled==1 && !strncmp(key,"xenia_zpatch_",13));
      CHECK(words[0]==initial[i] && words[1]==UINT32_C(0x39600000));
      CHECK(words[2]==UINT32_C(0x822F0DFC) && words[3]==UINT32_C(0x39600000));
      CHECK(xe_ng2_shadow_query(UINT32_C(0x544307D5),hashes[i],again,sizeof(again),words,&enabled)==1);
      CHECK(!strcmp(key,again));
      if(!i)strcpy(world,key);else CHECK(strcmp(world,key));
      CHECK(xe_ng2_shadow_forget());
   }
   CHECK(xe_ng2_shadow_query(UINT32_C(0x544307D5),UINT64_C(0x1DB607EDA25BFED7),key,sizeof(key),words,&enabled)==0);
   CHECK(xe_ng2_shadow_query(UINT32_C(0x544307D5),0,key,sizeof(key),words,&enabled)==0);
   CHECK(xe_ng2_shadow_query(UINT32_C(0x544307D5),UINT64_C(0xAFF931BFF00F8A0B),key,sizeof(key),words,&enabled)==0);
   CHECK(xe_ng2_shadow_query(UINT32_C(0x544307D1),hashes[0],key,sizeof(key),words,&enabled)==0);
   CHECK(xe_ng2_shadow_query(0,hashes[0],key,sizeof(key),words,&enabled)==0);
   CHECK(xe_ng2_shadow_forget());
   puts("PASS: real bundled NG2-only options, exact executable guards, defaults, stable keys and shadow-selector instructions");
   return 0;
}
