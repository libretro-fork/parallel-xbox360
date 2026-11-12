/* Test-only typed boundary for actual private primitive processing methods. */
#ifndef XE_TEST_PRIMITIVE_COUNTS_PROTOCOL_H
#define XE_TEST_PRIMITIVE_COUNTS_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#include "libretro/core_primitive_builtin.h"
#ifdef __cplusplus
extern "C" {
#endif
uint32_t xe_primitive_count_ranges(unsigned,unsigned,const void*,unsigned,
      uint32_t,uint32_t,uint32_t*,unsigned,unsigned*);
int xe_primitive_initialize(unsigned,void*,size_t,xe_primitive_builtin_layout*,size_t*,unsigned*,int);
/* Open once for warm measurements; close only after all synchronous calls
 * finish. NULL output avoids a test-only row copy, not real range generation.
 * With output present, capacity is in three-word records. No views retained. */
void *xe_primitive_counts_open(void);
void xe_primitive_counts_close(void*);
unsigned xe_primitive_counts_kind(void);
uint32_t xe_primitive_counts_run(void*,unsigned,unsigned,const void*,unsigned,
      uint32_t,uint32_t,uint32_t*,unsigned,unsigned*);
#ifdef __cplusplus
}
#endif
#endif
