/* Exact-size planning and direct final serialization of fixed color shapes. */
#define main color_dynamic_fixture_main
#include "transfer_color_spv_fixture.c"
#undef main
int main(int argc,char **argv){
 static const unsigned formats[8]={0,1,2,3,4,5,14,15};
 static const unsigned scales[3][2]={{1,1},{3,5},{4,4}};
 xe_spv_binary code={0},bad={0};xe_transfer_color_options options={0};
 xe_transfer_address_packet_config config={1,1,0};xe_transfer_address_key key={0};
 unsigned g,n,c,s,d,cases=0;size_t planned,old,minimum=(size_t)-1,maximum=0;
 char name[4096];FILE *file;
 CHECK(argc==2 && strlen(argv[1])<3800);
 key.source_color=key.dest_color=1;key.source_pitch=key.dest_pitch=1;
 for(g=0;g<3;++g)for(n=0;n<2;++n)for(c=0;c<(g?4u:1u);++c)
 for(s=0;s<8;++s)for(d=0;d<8;++d){
  config.scale_x=scales[g][0];config.scale_y=scales[g][1];config.native2=n;
  key.source_native2=key.dest_native2=n;
  key.source_scale_x=c&1?1:config.scale_x;key.source_scale_y=c&1?1:config.scale_y;
  key.dest_scale_x=c&2?1:config.scale_x;key.dest_scale_y=c&2?1:config.scale_y;
  key.source_wide=s==5 || s==7;key.dest_wide=d==5 || d==7;
  options.source_uint=s>=4;options.dest_uint=d>=4;
  allocations=0;planned=0;
  CHECK(xe_transfer_color_spv_shape_words(&planned,&options,&config,&key,formats[s],formats[d]) && !allocations && planned>5);
  old=code.capacity;
  CHECK(xe_transfer_color_spv_build_shape(&code,&options,&config,&key,formats[s],formats[d]));
  CHECK(code.count==planned && allocations==(planned>old?1u:0u));
  if(planned<minimum)minimum=planned;
  if(planned>maximum)maximum=planned;
  sprintf(name,"%s/shape-g%u-n%u-c%u-s%u-d%u.spv",argv[1],g,n,c,s,d);
  file=fopen(name,"wb");CHECK(file);CHECK(fwrite(code.words,4,code.count,file)==code.count);CHECK(!fclose(file));++cases;
 }
 CHECK(cases==1152);
 allocations=0;fail_at=1;
 CHECK(!xe_transfer_color_spv_build_shape(&bad,&options,&config,&key,formats[7],formats[7]) && allocations==1 && !bad.count && !bad.words);
 fail_at=0;xe_spv_binary_reset(&bad);CHECK(xe_transfer_color_spv_build_shape(&bad,&options,&config,&key,formats[7],formats[7]));
 planned=77;key.dest_msaa=1;allocations=0;
 CHECK(!xe_transfer_color_spv_shape_words(&planned,&options,&config,&key,formats[7],formats[7]) && planned==77 && !allocations);
 xe_spv_binary_close(&code);xe_spv_binary_close(&bad);
 printf("PASS: %u fixed color shapes, exact allocation-free planning, one final allocation or reused storage, %lu..%lu words, failure/retry and unsupported MSAA rejection\n",cases,(unsigned long)minimum,(unsigned long)maximum);return 0;
}
