/* Test-only Vulkan timestamp decoding. BSD license. MSVC C89. */
#ifndef XE_TEST_GPU_TIMESTAMP_MEASURE_H
#define XE_TEST_GPU_TIMESTAMP_MEASURE_H
#include <stdint.h>
#include <float.h>
/* Query layout: start, availability, end, availability (64-bit results).
 * host_seconds encloses submission and the completed fence. Reject ambiguous
 * multiple wraps and impossible spans; never substitute host time for GPU time.
 * Failure leaves the destination unchanged. Zero can mean timer quantization. */
static int xe_test_gpu_timestamp_seconds(const uint64_t result[4],unsigned bits,
 float period_ns,double host_seconds,double *seconds) {
 uint64_t mask,ticks;double scale,wrap,elapsed;
 if(!result || !seconds || bits<36 || bits>64 || !result[1] || !result[3])return 0;
 if(!(period_ns>0 && period_ns<=FLT_MAX) || !(host_seconds>=0 && host_seconds<=DBL_MAX))return 0;
 mask=bits==64?UINT64_MAX:(UINT64_C(1)<<bits)-1;
 if((result[0]&~mask) || (result[2]&~mask))return 0;
 scale=(double)period_ns*1e-9;
 wrap=(bits==64?18446744073709551616.0:(double)(UINT64_C(1)<<bits))*scale;
 if(!(wrap>host_seconds))return 0;
 ticks=(result[2]-result[0])&mask;elapsed=(double)ticks*scale;
 if(!(elapsed<=host_seconds))return 0;
 *seconds=elapsed;return 1;
}
#endif
