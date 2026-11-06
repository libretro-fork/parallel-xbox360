// Legacy map/vector reference and offline CPU measurement only.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include "core_resolve_owners.h"
#if defined(_MSC_VER)
#define BOUNDARY __declspec(noinline)
#else
#define BOUNDARY __attribute__((noinline,noipa))
#endif
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"owner reference line %d: %s\n",__LINE__,#x); std::abort(); } } while(0)
using Row=xe_resolve_owner_state;
struct Counts { size_t allocations=0,live=0,peak=0; };
static Counts counts;
template<class T> struct CountAllocator {
  using value_type=T;
  CountAllocator()=default;
  template<class U> CountAllocator(const CountAllocator<U>&) {}
  T* allocate(size_t n) { T*p=std::allocator<T>().allocate(n);++counts.allocations;counts.live+=n*sizeof(T);counts.peak=std::max(counts.peak,counts.live);return p; }
  void deallocate(T*p,size_t n) {counts.live-=n*sizeof(T);std::allocator<T>().deallocate(p,n);}
  template<class U> bool operator==(const CountAllocator<U>&)const{return true;}
};
template<bool Counted=false> struct Legacy {
  using Pair=std::pair<const uint64_t,Row>;
  using MapAlloc=std::conditional_t<Counted,CountAllocator<Pair>,std::allocator<Pair>>;
  using AgeAlloc=std::conditional_t<Counted,CountAllocator<uint32_t>,std::allocator<uint32_t>>;
  std::unordered_map<uint64_t,Row,std::hash<uint64_t>,std::equal_to<uint64_t>,MapAlloc> rows;
  Row* find(uint64_t key){auto it=rows.find(key);return it==rows.end()?nullptr:&it->second;}
  Row* get(uint64_t key){return &rows[key];}
  size_t size()const{return rows.size();}
  void prune(const xe_resolve_readback* pages,uint32_t frame){
    for(auto it=rows.begin();it!=rows.end();){
      const auto&r=it->second;
      uint32_t last=std::min((r.address+r.length-1)>>12,XE_MEMEXPORT_PAGE_COUNT-1);
      bool first_owned=xe_resolve_owner_load(pages,r.address>>12)==it->first;
      bool last_owned=xe_resolve_owner_load(pages,last)==it->first;
      if(r.in_guest_ram||(!first_owned&&!last_owned))it=rows.erase(it);else ++it;
    }
    if(rows.size()<4096)return;
    std::vector<uint32_t,AgeAlloc> ages;ages.reserve(rows.size());
    for(const auto&entry:rows)ages.push_back(frame-entry.second.frame);
    auto nth=ages.begin()+ages.size()*3/4;std::nth_element(ages.begin(),nth,ages.end());uint32_t age=*nth;
    for(auto it=rows.begin();it!=rows.end();)if(frame-it->second.frame>=age)it=rows.erase(it);else ++it;
  }
  void clear_rearm(){for(auto&entry:rows)entry.second.rearm_pending=0;}
};
struct Registry {
  std::unique_ptr<xe_resolve_owners_storage> rows{new xe_resolve_owners_storage};
  Row* find(uint64_t key){return xe_resolve_owners_find(rows.get(),key);}
  Row* get(uint64_t key){return xe_resolve_owners_get(rows.get(),key);}
  size_t size()const{return rows->count;}
  void prune(const xe_resolve_readback*pages,uint32_t frame){xe_resolve_owners_prune(rows.get(),pages,frame);}
  void clear_rearm(){xe_resolve_owners_clear_rearm(rows.get());}
};
static bool equal(const Row&a,const Row&b){
  return a.address==b.address&&a.length==b.length&&a.submission==b.submission&&a.frame==b.frame&&
    a.copyable==b.copyable&&a.in_guest_ram==b.in_guest_ram&&a.rearm_pending==b.rearm_pending&&
    a.clip_first_page==b.clip_first_page&&a.clip_last_page==b.clip_last_page;
}
static void verify(Legacy<>&old,Registry&now){
  CHECK(old.size()==now.size());
  for(const auto&e:old.rows){auto*r=now.find(e.first);CHECK(r&&equal(*r,e.second));}
}
static Row value(uint32_t slot,uint32_t frame){
  Row r{};r.address=slot*8192;r.length=8192;r.submission=UINT64_C(0xfedcba9800000000)+slot;
  r.frame=frame;r.copyable=1;r.rearm_pending=1;r.clip_first_page=uint8_t(slot&1);r.clip_last_page=uint8_t((slot>>1)&1);return r;
}
static uint64_t key_of(const Row&r){return (uint64_t(r.address)<<32)|r.length;}
static uint32_t rng=0x549137;
static uint32_t random32(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void reference(){
  xe_resolve_readback_owner pages;CHECK(xe_resolve_readback_init(&pages,0));
  size_t comparisons=0,prunes=0;
  for(unsigned mode=0;mode<7;++mode){
    Legacy<>old;Registry now;std::vector<std::pair<uint64_t,Row*>> leases;
    for(uint32_t i=0;i<4096;++i){
      Row r=value(i,mode==0?5:mode==1?5-i:mode==2?UINT32_MAX-i:random32());
      uint64_t key=key_of(r);*old.get(key)=r;auto*p=now.get(key);CHECK(p);*p=r;leases.emplace_back(key,p);
      xe_resolve_owner_set(&pages,r.address,r.length,key);
      if(mode==3&&!(i&1)){old.find(key)->in_guest_ram=now.find(key)->in_guest_ram=1;}
      if(mode==4||mode==5){xe_resolve_owner_set(&pages,r.address,4096,0);if(mode==5)xe_resolve_owner_set(&pages,r.address+4096,4096,0);}
    }
    old.prune(&pages,5);now.prune(&pages,5);++prunes;verify(old,now);++comparisons;
    CHECK(now.size()<4096);
    for(auto e:leases)if(old.find(e.first))CHECK(now.find(e.first)==e.second);
    old.clear_rearm();now.clear_rearm();verify(old,now);++comparisons;
  }
  Legacy<>old;Registry now;uint32_t frame=UINT32_MAX-1000;
  for(uint32_t step=0;step<100000;++step){
    uint32_t slot=random32()%6000;Row r=value(slot,frame);uint64_t key=key_of(r);
    if(step%17==0){if(auto*p=old.find(key)){p->in_guest_ram=1;now.find(key)->in_guest_ram=1;}}
    else {
      xe_resolve_owner_set(&pages,r.address,r.length,key);
      if(old.size()>=4096){old.prune(&pages,frame);now.prune(&pages,frame);++prunes;verify(old,now);++comparisons;}
      auto*p=now.get(key);CHECK(p);*p=r;*old.get(key)=r;
    }
    if(step%97==0){old.clear_rearm();now.clear_rearm();}
    if(step%251==0){verify(old,now);++comparisons;}
    frame+=random32()%4;
  }
  verify(old,now);
  std::printf("Owner reference: 100,000 updates, %zu full/edge/tie/frame-wrap prunes, %zu exact key/field snapshots and stable surviving leases pass\n",prunes,comparisons);
}
static volatile uint64_t sink;
template<class Model> static void prime(Model&m,xe_resolve_readback*pages){
  for(uint32_t i=0;i<4096;++i){Row r=value(i,0);uint64_t key=key_of(r);*m.get(key)=r;xe_resolve_owner_set(pages,r.address,r.length,key);}
}
template<class Model> static BOUNDARY uint64_t execute(Model&m,xe_resolve_readback*pages,unsigned kind,unsigned iterations){
  uint64_t sum=0;
  if(kind==0){for(uint32_t i=0;i<iterations;++i){Row r=value(i&4095,0);auto*p=m.find(key_of(r));CHECK(p);p->frame=i;sum+=p->submission;}}
  else if(kind==1){
    for(uint32_t i=0;i<iterations;++i){Row r=value(i%6000,i);auto key=key_of(r);xe_resolve_owner_set(pages,r.address,r.length,key);if(m.size()>=4096)m.prune(pages,i);auto*p=m.get(key);CHECK(p);*p=r;sum+=p->submission;}
  } else {
    m.prune(pages,0);CHECK(!m.size());
    for(uint32_t i=0;i<iterations;++i){prime(m,pages);m.prune(pages,i);CHECK(!m.size());sum+=4096;}
  }
  sink=sum;return sum;
}
static void benchmark(unsigned kind,const char*name,unsigned iterations){
  double a[7],b[7];
  xe_resolve_readback_owner pages;CHECK(xe_resolve_readback_init(&pages,0));
  for(unsigned sample=0;sample<7;++sample){
    auto old=std::make_unique<Legacy<>>();auto now=std::make_unique<Registry>();uint64_t x=0,y=0;
    auto run_old=[&]{prime(*old,&pages);auto t=std::chrono::steady_clock::now();x=execute(*old,&pages,kind,iterations);a[sample]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();};
    auto run_new=[&]{prime(*now,&pages);auto t=std::chrono::steady_clock::now();y=execute(*now,&pages,kind,iterations);b[sample]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();};
    if(sample&1){run_new();run_old();}else{run_old();run_new();}CHECK(x==y);
  }
  counts={};size_t warm_allocations,steady,peak;
  {Legacy<true>old;prime(old,&pages);warm_allocations=counts.allocations;execute(old,&pages,kind,iterations);steady=counts.live;peak=counts.peak;}
  CHECK(!counts.live);std::sort(a,a+7);std::sort(b,b+7);
  std::printf("%s: %u operations, map %.3f ms, C89 %.3f ms (%.2fx); separate untimed legacy operation allocations %zu, retained %zu, peak %zu; fixed C89 %zu bytes, no operation allocation\n",name,iterations,a[3],b[3],a[3]/b[3],counts.allocations-warm_allocations,steady,peak,sizeof(xe_resolve_owners));
}
int main(int argc,char**argv){
  reference();
  if(argc>1 && !std::strcmp(argv[1],"--check"))return 0;
#if defined(_MSC_VER)
  std::printf("MSVC %d /O2; ",_MSC_VER);
#else
  std::printf("GCC %s -O2; ",__VERSION__);
#endif
  std::puts("7 interleaved medians; standard allocators in timing, separately counted allocation trace; CPU only");
  benchmark(0,"hot-find-update",1000000);benchmark(1,"owner-churn",50000);benchmark(2,"all-tied-full-prune",100);
}
