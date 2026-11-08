#include "libretro/core_shader_names.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"shader names line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
static void check_name(const unsigned char *storage,const char *expected)
{
   size_t i,n=strlen(expected);
   CHECK(n<XE_SHADER_NAME_CAPACITY);
   CHECK(!strcmp((const char*)storage+8,expected));
   for(i=0;i<8;++i)CHECK(storage[i]==0xa5);
   for(i=8+n+1;i<XE_SHADER_NAME_CAPACITY+16;++i)CHECK(storage[i]==0xa5);
}
int main(void)
{
   static const uint32_t fetches[]={0,1,9,10,31,32,99,100,999,1000,
      UINT32_C(999999999),UINT32_C(1000000000),UINT32_C(4294967295)};
   static const char filters[]="plbf";
   static const char *dimensions[]={"2d","3d","cube","2d"};
   static const uint32_t ratios[]={0,1,2,4,8,16,UINT32_C(4294967295)};
   static const char *prefixes[]={"xe_var_memexport_data_","xe_out_interpolator_",
      "xe_var_rect_guest_interpolators_","xe_in_interpolator_","xe_in_interpolator_"};
   unsigned char storage[XE_SHADER_NAME_CAPACITY+16];
   char expected[80];
   unsigned f,d,s,mag,min,mip,a,use,count=0;
   for(f=0;f<sizeof(fetches)/sizeof(fetches[0]);++f) {
      for(d=0;d<5;++d) {
         memset(storage,0xa5,sizeof(storage));
         xe_shader_indexed_name((char*)storage+8,fetches[f],
               (xe_shader_indexed_name_kind)d);
         sprintf(expected,"%s%lu%s",prefixes[d],(unsigned long)fetches[f],
               d==4?"_per_vertex":"");
         check_name(storage,expected);++count;
      }
      for(d=0;d<4;++d)for(s=0;s<2;++s) {
         memset(storage,0xa5,sizeof(storage));
         xe_shader_texture_name((char*)storage+8,fetches[f],d,(int)s);
         sprintf(expected,"xe_texture%lu_%s_%c",(unsigned long)fetches[f],
               dimensions[d],s?'s':'u');
         check_name(storage,expected);++count;
      }
      for(mag=0;mag<4;++mag)for(min=0;min<4;++min)for(mip=0;mip<4;++mip)
      for(a=0;a<sizeof(ratios)/sizeof(ratios[0]);++a)for(use=0;use<2;++use) {
         memset(storage,0xa5,sizeof(storage));
         xe_shader_sampler_name((char*)storage+8,fetches[f],filters[mag],
               filters[min],filters[mip],(int)use,ratios[a]);
         if(use)sprintf(expected,"xe_sampler%lu_%c%c%c_a%lu",
               (unsigned long)fetches[f],filters[mag],filters[min],filters[mip],
               (unsigned long)ratios[a]);
         else sprintf(expected,"xe_sampler%lu_%c%c%c",(unsigned long)fetches[f],
               filters[mag],filters[min],filters[mip]);
         check_name(storage,expected);++count;
      }
   }
   printf("PASS: %u shader names, decimal boundaries and untouched guards\n",count);
   return 0;
}
