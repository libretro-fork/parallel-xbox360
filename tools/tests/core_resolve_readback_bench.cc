// Offline legacy/reference vector benchmark. No device or emulator invocation.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>
#include <vulkan/vulkan.h>
#include "core_resolve_readback.h"
#if defined(_MSC_VER)
#define BOUNDARY __declspec(noinline)
#else
#define BOUNDARY __attribute__((noinline, noipa))
#endif
struct Stats { size_t allocations = 0, live = 0, relocated = 0; };
template<class T> static Stats stats;
template<class T> struct Allocator {
  using value_type = T;
  Allocator() = default;
  template<class U> Allocator(const Allocator<U>&) {}
  T* allocate(size_t n) { T* p = std::allocator<T>().allocate(n); ++stats<T>.allocations; stats<T>.relocated += stats<T>.live; stats<T>.live += n*sizeof(T); return p; }
  void deallocate(T* p,size_t n) { stats<T>.live -= n*sizeof(T); std::allocator<T>().deallocate(p,n); }
  template<class U> bool operator==(const Allocator<U>&) const { return true; }
};
using Range = std::pair<uint32_t,uint32_t>;
static BOUNDARY uint64_t consume(const VkBufferCopy* records, size_t count) {
  uint64_t sum = 0;
  for (size_t i = 0; i < count; ++i) sum += records[i].srcOffset + records[i].dstOffset * 3 + records[i].size * 7;
  return sum;
}
static const uint64_t key = UINT64_C(0xfedcba9876543210);
struct Old {
  std::unique_ptr<std::atomic<uint64_t>[]> owners{new std::atomic<uint64_t>[XE_MEMEXPORT_PAGE_COUNT]{}};
  std::vector<Range,Allocator<Range>> ranges;
  std::vector<VkBufferCopy,Allocator<VkBufferCopy>> regions;
  BOUNDARY uint64_t query(uint32_t base,uint32_t length,bool packed) {
    uint32_t first = base>>12,last=(base+length-1)>>12;
    ranges.clear();
    for (uint32_t page=first;page<=last;++page) {
      if (owners[page].load(std::memory_order_relaxed)!=key) continue;
      uint32_t at=page<<12;
      if (!ranges.empty() && ranges.back().first+ranges.back().second==at) ranges.back().second+=4096;
      else ranges.emplace_back(at,4096);
    }
    if (!ranges.empty()) {
      if (ranges.front().first==(first<<12)) { ranges.front().second-=base-(first<<12); ranges.front().first=base; }
      auto& tail=ranges.back();
      if (tail.first+tail.second==((last+1)<<12)) tail.second-=((last+1)<<12)-(base+length);
    }
    regions.clear(); uint64_t offset=0;
    for (const auto& range:ranges) {
      auto& r=regions.emplace_back(); r.srcOffset=range.first; r.dstOffset=packed?offset:range.first; r.size=range.second; offset+=range.second;
    }
    return consume(regions.data(),regions.size());
  }
};
struct Snapshot {
  xe_resolve_readback_owner storage;
  xe_resolve_copy_plan plan;
  Snapshot() { if (!xe_resolve_readback_init(&storage,sizeof(VkBufferCopy))) std::abort(); }
  BOUNDARY uint64_t query(uint32_t base,uint32_t length,bool packed) {
    if (!xe_resolve_snapshot_make(&storage,key,base,length,1,1,packed,&plan)) return consume(nullptr,0);
    return consume(static_cast<const VkBufferCopy*>(plan.records),plan.count);
  }
};
static volatile uint64_t sink;
template<class T> static double measure(T& storage,uint32_t base,uint32_t length,unsigned repeats,uint64_t expected) {
  auto start=std::chrono::steady_clock::now();
  uint64_t sum=0;
  for (unsigned i=0;i<repeats;++i) { uint64_t value=storage.query(base,length,true); if(value!=expected) std::abort(); sum+=value; }
  sink=sum;
  return std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/repeats;
}
static void run(const char* name,uint32_t pages,unsigned pattern,unsigned repeats) {
  stats<Range> = {}; stats<VkBufferCopy> = {};
  Old old; Snapshot snapshot;
  for (uint32_t i=0;i<pages;++i) {
    uint64_t value=pattern==0 || (pattern==1 && !(i&1)) || (pattern==2 && (i==0 || i==pages-1))?key:1;
    old.owners[i].store(value,std::memory_order_relaxed);
    retro_atomic_store_relaxed_64(snapshot.storage.owners+i,(int64_t)value);
  }
  uint32_t base=31,length=(pages<<12)-98;
  for (unsigned packed=0;packed<2;++packed) {
    if(old.query(base,length,packed!=0)!=snapshot.query(base,length,packed!=0)) std::abort();
    if(old.regions.size()!=snapshot.plan.count) std::abort();
    const auto* r=static_cast<const VkBufferCopy*>(snapshot.storage.records);
    for(size_t i=0;i<old.regions.size();++i)
      if(old.regions[i].srcOffset!=r[i].srcOffset || old.regions[i].dstOffset!=r[i].dstOffset || old.regions[i].size!=r[i].size) std::abort();
  }
  size_t allocations=stats<Range>.allocations+stats<VkBufferCopy>.allocations;
  uint64_t expected=old.query(base,length,true);
  double a[7],b[7];
  for(unsigned sample=0;sample<7;++sample) {
    if(sample&1) { b[sample]=measure(snapshot,base,length,repeats,expected); a[sample]=measure(old,base,length,repeats,expected); }
    else { a[sample]=measure(old,base,length,repeats,expected); b[sample]=measure(snapshot,base,length,repeats,expected); }
  }
  if(allocations!=stats<Range>.allocations+stats<VkBufferCopy>.allocations) std::abort();
  std::sort(a,a+7);std::sort(b,b+7);
  std::printf("%s: %u pages, %zu runs; vectors %.3f us, snapshot %.3f us (%.2fx); cold vector growth allocations %zu/0, relocated %zu/0 bytes, retained range+native %zu/%zu bytes; warm growth 0/0\n",name,pages,old.regions.size(),a[3],b[3],a[3]/b[3],allocations,stats<Range>.relocated+stats<VkBufferCopy>.relocated,stats<Range>.live+stats<VkBufferCopy>.live,size_t(XE_RESOLVE_MAX_RUNS)*sizeof(VkBufferCopy));
}
int main() {
#if defined(_MSC_VER)
  std::printf("MSVC %d /O2; ",_MSC_VER);
#else
  std::printf("GCC %s -O2; ",__VERSION__);
#endif
  std::puts("7 interleaved samples, median snapshot+native record+CPU consume time; warm capacities; paired checksums/exact host+packed records; no device calls");
  std::puts("Cold allocation combines unchanged 1 MiB keys and 1.5 MiB final native capacity (compact D3D12/zero-copy: 512 KiB). No duplicate CPU range or bitmap array. Old keys allocate 1 MiB plus vector growth.");
  run("small-dense",8,0,100000);
  run("dense",4096,0,1000);
  run("fragmented",4096,1,500);
  run("sparse",65536,2,100);
  run("max-fragmented",XE_MEMEXPORT_PAGE_COUNT,1,20);
}
