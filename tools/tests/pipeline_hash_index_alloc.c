/* Test-only allocation-window accounting. Single-threaded fixture; C89. */
#include "pipeline_hash_index_fixture.h"
#include <stdlib.h>
static size_t allocations,live_allocations;
void *xe_pipeline_index_test_allocate(size_t bytes)
{
 void *p=malloc(bytes?bytes:1);
 if(p){++allocations;++live_allocations;}
 return p;
}
void xe_pipeline_index_test_free(void *p)
{
 if(p)--live_allocations;
 free(p);
}
void xe_pipeline_index_test_reset(void){allocations=0;}
size_t xe_pipeline_index_test_count(void){return allocations;}
size_t xe_pipeline_index_test_live(void){return live_allocations;}
