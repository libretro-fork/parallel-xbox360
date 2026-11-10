/* Availability, wrap/width, conversion and failure invariants. MSVC C89. */
#include "gpu_timestamp_measure.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"GPU timestamp math line %u\n",(unsigned)__LINE__);return 1;}}while(0)
static int near_value(double a,double b){double d=a-b;return (d<0?-d:d)<1e-18;}
int main(void) {
 uint64_t stamps[4]={100,1,200,1},bad_double=UINT64_C(0x7ff8000000000000);
 uint32_t bad_float=UINT32_C(0x7fc00000);double seconds=13,nan_host;float nan_period;
 volatile size_t host_size=sizeof(nan_host),period_size=sizeof(nan_period);
 unsigned bits;CHECK(host_size==8 && period_size==4);
 CHECK(xe_test_gpu_timestamp_seconds(stamps,64,2.5f,.001,&seconds) && near_value(seconds,250e-9));
 for(bits=36;bits<=64;++bits){
  uint64_t mask=bits==64?UINT64_MAX:(UINT64_C(1)<<bits)-1;
  stamps[0]=mask-4;stamps[2]=3;
  CHECK(xe_test_gpu_timestamp_seconds(stamps,bits,.25f,1,&seconds) && near_value(seconds,2e-9));
 }
 stamps[0]=stamps[2]=0;
 CHECK(xe_test_gpu_timestamp_seconds(stamps,64,1,0,&seconds) && seconds==0);
 seconds=13;CHECK(!xe_test_gpu_timestamp_seconds(NULL,64,1,1,&seconds) && seconds==13);
 CHECK(!xe_test_gpu_timestamp_seconds(stamps,64,1,1,NULL));
 for(bits=0;bits<36;++bits)CHECK(!xe_test_gpu_timestamp_seconds(stamps,bits,1,1,&seconds) && seconds==13);
 CHECK(!xe_test_gpu_timestamp_seconds(stamps,65,1,1,&seconds) && seconds==13);
 stamps[1]=0;CHECK(!xe_test_gpu_timestamp_seconds(stamps,64,1,1,&seconds) && seconds==13);stamps[1]=1;
 stamps[3]=0;CHECK(!xe_test_gpu_timestamp_seconds(stamps,64,1,1,&seconds) && seconds==13);stamps[3]=1;
 stamps[0]=UINT64_C(1)<<36;CHECK(!xe_test_gpu_timestamp_seconds(stamps,36,1,1,&seconds) && seconds==13);stamps[0]=0;
 stamps[2]=UINT64_C(1)<<36;CHECK(!xe_test_gpu_timestamp_seconds(stamps,36,1,1,&seconds) && seconds==13);stamps[2]=0;
 CHECK(!xe_test_gpu_timestamp_seconds(stamps,36,1,100,&seconds) && seconds==13);
 CHECK(!xe_test_gpu_timestamp_seconds(stamps,64,0,1,&seconds) && seconds==13);
 CHECK(!xe_test_gpu_timestamp_seconds(stamps,64,-1,1,&seconds) && seconds==13);
 memcpy(&nan_period,&bad_float,4);memcpy(&nan_host,&bad_double,8);
 CHECK(!xe_test_gpu_timestamp_seconds(stamps,64,nan_period,1,&seconds) && seconds==13);
 bad_float=UINT32_C(0x7f800000);memcpy(&nan_period,&bad_float,4);
 CHECK(!xe_test_gpu_timestamp_seconds(stamps,64,nan_period,1,&seconds) && seconds==13);
 CHECK(!xe_test_gpu_timestamp_seconds(stamps,64,1,nan_host,&seconds) && seconds==13);
 CHECK(!xe_test_gpu_timestamp_seconds(stamps,64,1,-1,&seconds) && seconds==13);
 stamps[2]=100;CHECK(!xe_test_gpu_timestamp_seconds(stamps,64,1,1e-9,&seconds) && seconds==13);
 puts("PASS: GPU query availability, widths36-64, modular wraps, fractional periods, zero quantization and unchanged failure destination");return 0;
}
