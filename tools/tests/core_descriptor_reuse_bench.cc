/* Offline CPU metadata benchmark; no driver, frontend, JIT or game. */
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <memory>
#include <vector>
#include <utility>
static size_t old_allocations,new_allocations;
static void* page_allocate(size_t n) { ++new_allocations; return std::malloc(n); }
#define XE_DESCRIPTOR_REUSE_ALLOC page_allocate
#define XE_DESCRIPTOR_REUSE_FREE std::free
#include "core_descriptor_reuse.h"
template<class T> struct CountAllocator {
  using value_type=T;
  CountAllocator()=default;
  template<class U> CountAllocator(const CountAllocator<U>&) {}
  T* allocate(size_t n) { ++old_allocations; return std::allocator<T>{}.allocate(n); }
  void deallocate(T* p,size_t n) { std::allocator<T>{}.deallocate(p,n); }
  template<class U> bool operator==(const CountAllocator<U>&) const { return true; }
  template<class U> bool operator!=(const CountAllocator<U>&) const { return false; }
};
struct Used { uint64_t frame; unsigned layout; uint64_t handle; };
struct Result { double ms; size_t allocations; uint64_t sum; };
static Result legacy() {
  std::deque<Used,CountAllocator<Used>> used;
  using Constant=std::pair<uint64_t,uint64_t>;
  std::deque<Constant,CountAllocator<Constant>> constants;
  std::vector<uint64_t,CountAllocator<uint64_t>> free[3];
  uint64_t id=0,sum=0;
  auto step=[&](uint64_t frame) {
    uint64_t completed=frame>2 ? frame-2 : 0;
    while(!used.empty() && used.front().frame<=completed) {
      free[used.front().layout].push_back(used.front().handle); used.pop_front();
    }
    while(!constants.empty() && constants.front().first<=completed) {
      free[2].push_back(constants.front().second); constants.pop_front();
    }
    for(unsigned draw=0;draw<64;++draw) {
      unsigned kind=draw%3;
      uint64_t handle;
      if(free[kind].empty()) handle=++id;
      else { handle=free[kind].back(); free[kind].pop_back(); }
      if(kind==2) constants.emplace_back(frame,handle);
      else used.push_back({frame,kind,handle});
      sum^=handle+frame*131+kind;
    }
  };
  for(uint64_t f=1;f<=1000;++f) step(f);
  size_t before=old_allocations;
  std::clock_t start=std::clock();
  for(uint64_t f=1001;f<=501000;++f) step(f);
  double ms=1000.0*(std::clock()-start)/CLOCKS_PER_SEC;
  return {ms,old_allocations-before,sum};
}
static Result stable() {
  xe_descriptor_reuse pools[3]{};
  uint64_t id=0,sum=0;
  for(auto& pool:pools) if(!xe_descriptor_reuse_warm(&pool)) std::abort();
  auto step=[&](uint64_t frame) {
    uint64_t completed=frame>2 ? frame-2 : 0;
    for(auto& pool:pools) xe_descriptor_reuse_collect(&pool,completed);
    for(unsigned draw=0;draw<64;++draw) {
      unsigned kind=draw%3;
      auto* node=xe_descriptor_reuse_acquire(pools+kind,frame);
      if(!node) std::abort();
      if(!node->handle) node->handle=++id;
      sum^=node->handle+frame*131+kind;
      if(!xe_descriptor_reuse_commit(pools+kind)) std::abort();
    }
  };
  for(uint64_t f=1;f<=1000;++f) step(f);
  size_t before=new_allocations;
  std::clock_t start=std::clock();
  for(uint64_t f=1001;f<=501000;++f) step(f);
  double ms=1000.0*(std::clock()-start)/CLOCKS_PER_SEC;
  size_t count=new_allocations-before;
  for(auto& pool:pools) xe_descriptor_reuse_destroy(&pool);
  return {ms,count,sum};
}
int main() {
  double old_times[3],new_times[3];
  size_t old_count=0,new_count=0;
  for(unsigned i=0;i<3;++i) {
    Result a=legacy(),b=stable();
    if(a.sum!=b.sum) return 1;
    old_times[i]=a.ms; new_times[i]=b.ms; old_count=a.allocations; new_count=b.allocations;
  }
  std::sort(old_times,old_times+3); std::sort(new_times,new_times+3);
  std::printf("32M leases, two frames pending, three pools, 1000 warm-up frames; median of 3 CPU runs\n"
      "legacy %.3f ms, %zu metadata allocations; C89 %.3f ms, %zu metadata allocations; ratio %.2fx\n",
      old_times[1],old_count,new_times[1],new_count,old_times[1]/new_times[1]);
  return new_count ? 1 : 0;
}
