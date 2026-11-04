#include "xenia/gpu/spirv/spvIR.h"
#include "core_pipeline_bench_profile.h"
#include <new>
#include <cstdio>
#include <cstdlib>
#ifndef XE_SPV_BENCH_PLAIN
void *operator new(std::size_t n) { void *p=xe_bench_malloc(n); if(!p) throw std::bad_alloc(); return p; }
void *operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void *p) noexcept { xe_bench_free(p); }
void operator delete[](void *p) noexcept { xe_bench_free(p); }
void operator delete(void *p, std::size_t) noexcept { xe_bench_free(p); }
void operator delete[](void *p, std::size_t) noexcept { xe_bench_free(p); }
#endif
static volatile uint64_t checksum;
static void measure(const char *label, unsigned words) {
  const unsigned jobs=200000;
  uint64_t samples[5], sum=0;
  size_t allocations=0;
  std::vector<unsigned> out; out.reserve(64);
  for(unsigned round=0;round<5;++round) {
    const size_t before=xe_bench_allocations();
    const uint64_t begin=xe_bench_time();
    for(unsigned j=0;j<jobs;++j) {
      spv::Instruction inst(j+1, 3, spv::Op::OpIAdd);
      for(unsigned w=0;w<words;++w) {
        if(w&1) inst.addIdOperand(j+w+1); else inst.addImmediateOperand(j+w);
      }
      out.clear(); inst.dump(out);
      for(unsigned w:out) sum+=w;
    }
    samples[round]=xe_bench_time()-begin;
    allocations+=xe_bench_allocations()-before;
  }
  checksum=sum;
  xe_bench_report(label,words,0,sizeof(spv::Instruction),samples,5,jobs);
#ifndef XE_SPV_BENCH_PLAIN
  std::printf("allocations/instruction=%.1f checksum=%llu\n", double(allocations)/(jobs*5), (unsigned long long)sum);
#else
  std::printf("untracked allocations; checksum=%llu\n", (unsigned long long)sum);
#endif
}
int main(int argc,char **argv) {
  if(argc!=2)return 1;
  for(unsigned n:{0u,1u,2u,4u,8u,20u}) measure(argv[1],n);
  return xe_bench_live()!=0;
}
