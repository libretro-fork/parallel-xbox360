// Exact legacy policy and offline CPU/allocation measurements; test-only C++.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include "core_resolve_recent.h"
#define CHECK(x) do { if(!(x)) {std::fprintf(stderr,"recent reference line %d: %s\n",__LINE__,#x);std::abort();} } while(0)
#if defined(_MSC_VER)
#define BOUNDARY __declspec(noinline)
#else
#define BOUNDARY __attribute__((noinline,noipa))
#endif
struct Counts {size_t n=0,live=0,peak=0;};
static Counts counts;
template<class T> struct Counting {
  using value_type=T;
  Counting()=default;
  template<class U> Counting(const Counting<U>&){}
  T* allocate(size_t n){auto*p=std::allocator<T>().allocate(n);++counts.n;counts.live+=n*sizeof(T);counts.peak=std::max(counts.peak,counts.live);return p;}
  void deallocate(T*p,size_t n){counts.live-=n*sizeof(T);std::allocator<T>().deallocate(p,n);}
  template<class U> bool operator==(const Counting<U>&)const{return true;}
};
template<bool Counted=false> struct Legacy {
  using Pair=std::pair<const uint64_t,uint32_t>;
  using Alloc=std::conditional_t<Counted,Counting<Pair>,std::allocator<Pair>>;
  std::unordered_map<uint64_t,uint32_t,std::hash<uint64_t>,std::equal_to<uint64_t>,Alloc> rows;
  unsigned prunes=0,clears=0;
  BOUNDARY bool read(uint64_t key,uint32_t frame,bool before){
    if(before){
      if(rows.size()>=4096){
        ++prunes;
        for(auto it=rows.begin();it!=rows.end();)if(frame-it->second>60)it=rows.erase(it);else ++it;
        if(rows.size()>=4096){rows.clear();++clears;}
      }
      rows[key]=frame;return true;
    }
    auto it=rows.find(key);return it!=rows.end()&&frame-it->second<=60;
  }
};
struct Modern {
  xe_resolve_recent_storage rows;
  BOUNDARY bool read(uint64_t key,uint32_t frame,bool before){return xe_resolve_recent_read(&rows,key,frame,before)!=0;}
};
static uint32_t random_word=0x4179ba53u;
static uint32_t next_word(){random_word^=random_word<<13;random_word^=random_word>>17;random_word^=random_word<<5;return random_word;}
static uint64_t key_of(uint32_t id){return (uint64_t(id*8192u)<<32)|(4096u+(id%13)*256u);}
static void snapshot(const Legacy<>&old,const Modern&now){
  CHECK(old.rows.size()==now.rows.count);
  uint32_t live=0;
  for(uint32_t i=0;i<XE_RESOLVE_RECENT_BUCKETS;++i)if(xe_resolve_recent_live(&now.rows,i)){
    ++live;auto it=old.rows.find(now.rows.keys[i]);CHECK(it!=old.rows.end());CHECK(it->second==now.rows.frames[i]);
    CHECK(xe_resolve_recent_slot(&now.rows,it->first)==i);
  }
  CHECK(live==old.rows.size());
}
static void correctness(){
  Legacy<> old;Modern now;unsigned snapshots=0,events=0;
  auto event=[&](uint64_t key,uint32_t frame,bool before){
    const unsigned prunes=old.prunes;
    CHECK(old.read(key,frame,before)==now.read(key,frame,before));++events;
    if(old.prunes!=prunes||events%997==0){snapshot(old,now);++snapshots;}
  };
  // Repeated full/expiry/clear cases include threshold ties and uint32 wrap.
  for(unsigned pass=0;pass<12;++pass){
    old.rows.clear();xe_resolve_recent_reset(&now.rows);
    const uint32_t base=pass&1?UINT32_MAX-30u:100u;
    for(uint32_t i=0;i<4096;++i)event(key_of(i),base+(i&1?61u:0u),true);
    event(key_of(0),base+61u,false);
    event(key_of(1),base+61u,true);
    for(uint32_t i=0;i<4096;++i)event(key_of(i),base+61u,false);
    for(uint32_t i=0;i<4096;i+=2)event(key_of(i),base+61u,true);
    event(key_of(3),base+121u,true); // Age exactly 60: clear even for existing key.
    CHECK(old.rows.size()==1);
  }
  for(uint32_t i=0;i<300000;++i){
    uint32_t r=next_word();uint64_t key=key_of(r%6500);
    if(i%1009==0)key=i&1?0:UINT64_MAX;
    event(key,UINT32_MAX-600u+i/97,(r&7)!=0);
  }
  snapshot(old,now);CHECK(old.prunes>20);CHECK(old.clears>=12);
  std::printf("Recent reference: %u events, %u prunes, %u clears, %u exact snapshots pass (expiry ties, existing-key full update, all-bit keys, frame wrap)\n",events,old.prunes,old.clears,snapshots+1);
}
template<class T> static uint64_t workload(T&model,bool churn){
  uint64_t sum=0;
  const uint32_t n=churn?200000u:1000000u;
  for(uint32_t i=0;i<n;++i){
    const uint32_t id=churn?i%6000:(i*173u)%4095;
    sum+=model.read(key_of(id),churn?i/61:1,(i&3)!=0);
  }
  return sum;
}
template<class T> static void prime(T&m){for(uint32_t i=0;i<4095;++i)m.read(key_of(i),0,true);}
static void benchmark(bool churn){
  double a[7],b[7];
  for(unsigned i=0;i<7;++i){
    Legacy<> old;Modern now;prime(old);prime(now);uint64_t x=0,y=0;
    auto time=[&](auto&m,uint64_t&sum){auto t=std::chrono::steady_clock::now();sum=workload(m,churn);return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();};
    if(i&1){b[i]=time(now,y);a[i]=time(old,x);}else{a[i]=time(old,x);b[i]=time(now,y);}CHECK(x==y);
  }
  std::sort(a,a+7);std::sort(b,b+7);
  counts={};size_t n,live,peak;
  {Legacy<true> old;prime(old);counts.n=0;counts.peak=counts.live;workload(old,churn);n=counts.n;live=counts.live;peak=counts.peak;}
  CHECK(counts.live==0);
  std::printf("%s: map %.3f ms, fixed %.3f ms (%.2fx); untimed map operation allocations=%zu retained=%zu peak=%zu, fixed=%zu no operation allocation\n",churn?"churn200k":"hot1M",a[3],b[3],a[3]/b[3],n,live,peak,sizeof(xe_resolve_recent));
}
int main(int argc,char**argv){correctness();if(argc>1&&std::strcmp(argv[1],"--check")==0)return 0;
  std::puts("Seven interleaved medians, standard allocators in timed runs, separately counted trace; CPU only");benchmark(false);benchmark(true);return 0;}
