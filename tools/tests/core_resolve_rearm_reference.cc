// Offline legacy-vector comparison; no native watch/protection operation.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>
#include <cstring>
#include "core_resolve_rearm.h"
#define CHECK(x) do { if(!(x))std::abort(); } while(0)
#if defined(_MSC_VER)
#define BOUNDARY __declspec(noinline)
#else
#define BOUNDARY __attribute__((noinline,noipa))
#endif
struct Range {uint32_t base,size;};
static size_t allocations;
template<class T>struct Counting {
  using value_type=T;Counting()=default;template<class U>Counting(const Counting<U>&){}
  T*allocate(size_t n){++allocations;return std::allocator<T>().allocate(n);}
  void deallocate(T*p,size_t n){std::allocator<T>().deallocate(p,n);}
  template<class U>bool operator==(const Counting<U>&)const{return true;}
};
static uint64_t calls,bytes;
static BOUNDARY void arm(uint32_t base,uint32_t size){CHECK(base<XE_MEMEXPORT_BUFFER_SIZE);++calls;bytes+=size;}
template<class Alloc=std::allocator<Range>>struct Legacy {
  std::vector<Range,Alloc> pending,draining;
  BOUNDARY void mark(uint32_t base,uint32_t size){pending.push_back({base,size});}
  BOUNDARY void drain(){pending.swap(draining);for(const auto&r:draining)arm(r.base,r.size);draining.clear();}
};
struct Modern {
  xe_resolve_rearm_storage state;
  BOUNDARY void mark(uint32_t base,uint32_t size){CHECK(xe_resolve_rearm_mark(&state,base,size));}
  BOUNDARY void drain(){xe_memexport_cursor cursor;xe_memexport_run run;
    if(!xe_resolve_rearm_begin(&state,&cursor))return;
    while(xe_memexport_next(&cursor,&run))arm(run.base,run.size);
    xe_resolve_rearm_finish(&state);
  }
};
template<class T>static void batch(T&s,unsigned mode,unsigned count){
  for(uint32_t i=0;i<count;++i){uint32_t page=mode==0?i:(mode==1?i*997%131072:i%32);s.mark(page*4096+17,29);}
  s.drain();
}
static void coverage(){
  Legacy<>old;Modern now;static unsigned char a[XE_MEMEXPORT_PAGE_COUNT],b[XE_MEMEXPORT_PAGE_COUNT];
  uint32_t random=0x1a92fb37;
  for(unsigned pass=0;pass<1000;++pass){
    for(unsigned i=0;i<600;++i){random^=random<<13;random^=random>>17;random^=random<<5;
      const uint32_t base=random%(XE_MEMEXPORT_BUFFER_SIZE-8192),size=random%8192+1;
      old.mark(base,size);now.mark(base,size);
    }
    old.pending.swap(old.draining);xe_memexport_cursor cursor;xe_memexport_run run;
    CHECK(xe_resolve_rearm_begin(&now.state,&cursor));
    std::memset(a,0,sizeof(a));std::memset(b,0,sizeof(b));
    for(const auto&r:old.draining)for(uint32_t p=r.base/4096;p<=(r.base+r.size-1)/4096;++p)a[p]=1;
    // New marks while draining belong exclusively to the following batch.
    for(unsigned i=0;i<50;++i){uint32_t base=(pass*131+i*263)%131072*4096;old.mark(base,4096);now.mark(base,4096);}
    while(xe_memexport_next(&cursor,&run))for(uint32_t p=run.base/4096;p<(run.base+run.size)/4096;++p)b[p]=1;
    CHECK(std::equal(a,a+XE_MEMEXPORT_PAGE_COUNT,b));old.draining.clear();xe_resolve_rearm_finish(&now.state);
  }
  puts("Rearm legacy vectors: 1000 exact page-union snapshots and 50000 next-batch marks pass");
}
static void benchmark(unsigned mode,unsigned count,const char*name){
  double a[7],b[7];uint64_t oldcalls=0,newcalls=0;
  for(unsigned rep=0;rep<7;++rep){Legacy<>old;Modern now;
    batch(old,mode,count);batch(old,mode,count);batch(now,mode,count);batch(now,mode,count);
    auto time=[&](auto&s,uint64_t&out){calls=bytes=0;auto t=std::chrono::steady_clock::now();
      for(unsigned i=0;i<500;++i)batch(s,mode,count);
      const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();out=calls;CHECK(bytes);return ms;};
    if(rep&1){b[rep]=time(now,newcalls);a[rep]=time(old,oldcalls);}else{a[rep]=time(old,oldcalls);b[rep]=time(now,newcalls);}
  }
  std::sort(a,a+7);std::sort(b,b+7);
  allocations=0;Legacy<Counting<Range>>cold;
  for(unsigned i=0;i<500;++i)batch(cold,mode,count);
  const size_t coldalloc=allocations;allocations=0;
  for(unsigned i=0;i<500;++i)batch(cold,mode,count);
  printf("%s: vector %.3f ms, bitmap %.3f ms (%.2fx), calls %llu -> %llu; separate cold allocations %zu, warm %zu; fixed %zu bytes\n",name,a[3],b[3],a[3]/b[3],(unsigned long long)oldcalls,(unsigned long long)newcalls,coldalloc,allocations,sizeof(xe_resolve_rearm));
}
int main(int argc,char**argv){coverage();if(argc>1&&std::strcmp(argv[1],"--check")==0)return 0;
  puts("7 interleaved medians; 500 batches, warm capacity; noinline call proxy only, no native watches/GPU. Standard allocator timings; separate allocation trace.");
  benchmark(0,4096,"adjacent4096");benchmark(1,128,"sparse128");benchmark(2,4096,"duplicate4096");return 0;}
