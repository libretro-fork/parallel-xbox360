/* Same production API workload for old/new indexes. C89; test-only timing. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static uint64_t variant_ticks(void){LARGE_INTEGER n;QueryPerformanceCounter(&n);return (uint64_t)n.QuadPart;}
static uint64_t variant_frequency(void){LARGE_INTEGER n;QueryPerformanceFrequency(&n);return (uint64_t)n.QuadPart;}
#else
#include <time.h>
static uint64_t variant_ticks(void){struct timespec n;clock_gettime(CLOCK_MONOTONIC,&n);return (uint64_t)n.tv_sec*1000000000u+n.tv_nsec;}
static uint64_t variant_frequency(void){return 1000000000u;}
#endif
int main(int argc,char **argv) {
 static const unsigned sizes[6]={1,4,16,64,256,512};
 fixture f={0};void *owner,*p,*values[512];FILE *csv;char path[768];unsigned c,i,j,n,mode,which;
 uint64_t begin,end,frequency=variant_frequency(),sum=0,identity[512];
 size_t initial=live,owner_size,value_size,allocations,bytes,peak_live;int fresh;
 CHECK(argc==2 && strlen(argv[1])<700);sprintf(path,"%s/variant-index.csv",argv[1]);
 csv=fopen(path,"wb");CHECK(csv);
 xe_variant_sizes(&owner_size,&value_size);
 fprintf(csv,"mode,variants,operations,ticks,frequency,allocations,requested_bytes,retained_bytes,owner_size,value_size,checksum\n");
 for(c=0;c<6;++c){
  n=sizes[c];allocations=calls;bytes=requested;begin=variant_ticks();
  for(j=0;j<64;++j){
   owner=xe_variant_open(&f);
   for(i=0;i<n;++i){CHECK(xe_variant_create(owner,key_for(i),&fresh,&p)==1 && fresh);sum+=xe_variant_serial(p);}
   xe_variant_close(owner);CHECK(live==initial);
  }
  end=variant_ticks();
  fprintf(csv,"create_destroy,%u,%u,%llu,%llu,%lu,%lu,0,%lu,%lu,%llu\n",n,64*n,
   (unsigned long long)(end-begin),(unsigned long long)frequency,(unsigned long)(calls-allocations),
   (unsigned long)(requested-bytes),(unsigned long)owner_size,(unsigned long)value_size,(unsigned long long)sum);
  owner=xe_variant_open(&f);for(i=0;i<n;++i){CHECK(xe_variant_create(owner,key_for(i),NULL,&values[i])==1);identity[i]=xe_variant_serial(values[i]);}
  peak_live=live-initial;
  for(mode=0;mode<3;++mode){
   allocations=calls;bytes=requested;sum=0;begin=variant_ticks();
   for(i=0;i<524288;++i){
    which=mode==2?n-1:(i*73u+(i>>9))%n;
    if(mode==0)p=xe_variant_find(owner,key_for(which));
    else{CHECK(xe_variant_create(owner,key_for(which),NULL,&p)==1);}
    CHECK(p==values[which]);sum+=xe_variant_serial(p);
   }
   end=variant_ticks();CHECK(calls==allocations && requested==bytes);
   fprintf(csv,"%s,%u,524288,%llu,%llu,0,0,%lu,%lu,%lu,%llu\n",mode==0?"find":mode==1?"get_or_create":"repeat_get_or_create",n,
    (unsigned long long)(end-begin),(unsigned long long)frequency,(unsigned long)peak_live,
    (unsigned long)owner_size,(unsigned long)value_size,(unsigned long long)sum);
   CHECK(sum);
  }
  /* Independent serial/identity oracle after the timed read workload. */
  for(i=0;i<n;++i)CHECK(xe_variant_find(owner,key_for(i))==values[i] && xe_variant_serial(values[i])==identity[i]);
  xe_variant_close(owner);CHECK(live==initial);
 }
 CHECK(!fclose(csv));printf("PASS: actual Shader variant profile; 24 measured groups, checked owners/payloads and allocation accounting; intrusive=%u\n",xe_variant_intrusive());return 0;
}
