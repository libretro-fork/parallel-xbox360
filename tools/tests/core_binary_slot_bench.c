/* Synthetic metadata-only comparison; not a renderer/GPU benchmark. C89. */
#include <features/features_cpu.h>
#include <stdio.h>
#include <stdlib.h>
void xe_binary_reference_init(void);
int xe_binary_reference_old(unsigned);
int xe_binary_reference_slot(unsigned);
static double run(int (*query)(unsigned),unsigned mask,unsigned *sum) {
   unsigned i,count=0;
   int64_t start=cpu_features_get_time_usec();
   for(i=0;i<2000000;++i)count+=(unsigned)query((i*17)&mask);
   *sum=count;
   return (double)(cpu_features_get_time_usec()-start)*1000.0/2000000.0;
}
int main(void) {
   unsigned masks[3]={0,31,2047},i,old_sum,new_sum;
   double old_ns,new_ns;
   xe_binary_reference_init();
   for(i=0;i<3;++i) {
      old_ns=run(xe_binary_reference_old,masks[i],&old_sum);
      new_ns=run(xe_binary_reference_slot,masks[i],&new_sum);
      if(old_sum!=new_sum)abort();
      printf("keys=%u legacy=%.2f ns slot=%.2f ns checksum=%u\n",masks[i]+1,old_ns,new_ns,new_sum);
   }
   return 0;
}
