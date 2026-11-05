/* C89 regression: two-pass invocations borrow immutable transfer records. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro/core_gpu_worklist.h"
#include "libretro/core_gpu_transfer.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %u\n",(unsigned)__LINE__); exit(1); } } while (0)
typedef struct transfer { uint32_t start, end; const void *source, *host; } transfer;
typedef struct copied_invocation { transfer transfer; uint32_t key; } copied_invocation;
typedef char view_layout_check[sizeof(xe_gpu_transfer_ref)==sizeof(void *)+2*sizeof(uint32_t) ? 1 : -1];
typedef char view_compact_check[sizeof(xe_gpu_transfer_ref)<sizeof(copied_invocation) ? 1 : -1];
static transfer originals[2048], snapshot[2048];
static copied_invocation reference[4096];
static unsigned tokens[8];
typedef struct descriptor_pair { uint64_t cpu, gpu; } descriptor_pair;
static int compare_old(const void *a, const void *b)
{
   const copied_invocation *x=(const copied_invocation *)a, *y=(const copied_invocation *)b;
   if (x->key!=y->key) return x->key<y->key ? -1 : 1;
   if (x->transfer.source!=y->transfer.source) return
      (const unsigned *)x->transfer.source<(const unsigned *)y->transfer.source ? -1 : 1;
   return x->transfer.start<y->transfer.start ? -1 : x->transfer.start!=y->transfer.start;
}
static int compare_ref(const void *a, const void *b)
{
   const xe_gpu_transfer_ref *x=(const xe_gpu_transfer_ref *)a, *y=(const xe_gpu_transfer_ref *)b;
   const transfer *tx=(const transfer *)x->transfer, *ty=(const transfer *)y->transfer;
   if (x->shader_key!=y->shader_key) return x->shader_key<y->shader_key ? -1 : 1;
   if (tx->source!=ty->source) return (const unsigned *)tx->source<(const unsigned *)ty->source ? -1 : 1;
   return tx->start<ty->start ? -1 : tx->start!=ty->start;
}
int main(void)
{
   xe_gpu_worklist list = {0}, cpu = {0}, gpu = {0};
   xe_gpu_transfer_ref *ref, *next;
   const transfer *record, *following;
   void *stable;
   uint32_t round,i,j,n,seed=1,checks=0;
   int expected;
   CHECK(xe_gpu_worklist_init(&list,4096,sizeof(xe_gpu_transfer_ref)));
   stable=list.data;
   for (round=0;round<256;++round) {
      xe_gpu_worklist_reset(&list);
      for (i=0;i<2048;++i) {
         seed=seed*1664525u+1013904223u;
         originals[i].start=(i+(round*31))&2047; originals[i].end=originals[i].start+1;
         originals[i].source=&tokens[(seed>>16)&3];
         originals[i].host=(seed&4) ? &tokens[4+((seed>>8)&3)] : NULL;
      }
      memcpy(snapshot,originals,sizeof(snapshot));
      n=0;
      for (j=0;j<2;++j) for (i=0;i<2048;++i) {
         ref=(xe_gpu_transfer_ref *)xe_gpu_worklist_append(&list); CHECK(ref);
         xe_gpu_transfer_ref_set(ref,&originals[i],((i+round)%7)+j*8,j!=0);
         reference[n].transfer=originals[i]; reference[n].key=ref->shader_key;
         if (j) reference[n].transfer.host=NULL;
         ++n;
      }
      CHECK(n==4096 && list.count==n && list.data==stable);
      /* Sorting moves only invocation metadata, never the borrowed records. */
      qsort(list.data,n,sizeof(xe_gpu_transfer_ref),compare_ref);
      qsort(reference,n,sizeof(copied_invocation),compare_old);
      ref=(xe_gpu_transfer_ref *)list.data;
      for (i=0;i<n;++i) {
         record=(const transfer *)ref[i].transfer;
         CHECK(record->start==reference[i].transfer.start && record->end==reference[i].transfer.end);
         CHECK(record->source==reference[i].transfer.source && ref[i].shader_key==reference[i].key);
         CHECK(xe_gpu_transfer_host_depth(&ref[i],record->host)==reference[i].transfer.host);
         if (i+1<n) {
            next=&ref[i+1]; following=(const transfer *)next->transfer;
            expected=reference[i].key==reference[i+1].key &&
               reference[i].transfer.source==reference[i+1].transfer.source &&
               reference[i].transfer.host==reference[i+1].transfer.host;
            CHECK(xe_gpu_transfer_mergeable(&ref[i],record->source,record->host,
               next,following->source,following->host)==expected);
         }
         ++checks;
      }
      CHECK(!memcmp(originals,snapshot,sizeof(snapshot)));
      CHECK(!xe_gpu_worklist_append(&list) && list.failed);
   }
   xe_gpu_worklist_reset(&list); CHECK(!list.failed && !list.count && list.data==stable);
   xe_gpu_worklist_close(&list); xe_gpu_worklist_close(&list);
   /* D3D12's conservative 5 destinations * 2048 tiles * 3 SRVs. */
   CHECK(xe_gpu_worklist_init(&cpu,30720,sizeof(uint64_t)));
   CHECK(xe_gpu_worklist_init(&gpu,30720,sizeof(descriptor_pair)));
   stable=gpu.data;
   for (j=0;j<8;++j) {
      xe_gpu_worklist_reset(&cpu); xe_gpu_worklist_reset(&gpu);
      for (i=0;i<30720;++i) {
         uint64_t *input=(uint64_t *)xe_gpu_worklist_append(&cpu);
         descriptor_pair *output=(descriptor_pair *)xe_gpu_worklist_append(&gpu);
         CHECK(input && output);
         *input=((uint64_t)j<<32)|i;
         output->cpu=*input; output->gpu=(*input)^((uint64_t)1<<40);
      }
      CHECK(cpu.count==gpu.count && gpu.data==stable);
      CHECK(!xe_gpu_worklist_append(&cpu) && !xe_gpu_worklist_append(&gpu));
      for (i=0;i<30720;++i) {
         const descriptor_pair *output=(const descriptor_pair *)gpu.data+i;
         CHECK(output->cpu==((uint64_t *)cpu.data)[i]);
         CHECK(output->gpu==(output->cpu^((uint64_t)1<<40)));
      }
   }
   xe_gpu_worklist_close(&cpu); xe_gpu_worklist_close(&gpu);
   printf("PASS: %u borrowed transfer invocations, full two-pass capacity, old ordering/merge equivalence, immutable sources and overflow/reset\n",checks);
   puts("PASS: 245760 reusable descriptor pairs, full 30720-slot capacity, stable storage and overflow rejection");
   return 0;
}
