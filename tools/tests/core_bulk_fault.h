/* Forced include for the test object only. Keep CRT dllimport declarations
 * intact instead of renaming imported malloc/free declarations on MSVC. */
#include <stdlib.h>
#include <rthreads/rthreads.h>
void *bulk_test_malloc(size_t size);
void bulk_test_free(void *pointer);
sthread_t *bulk_test_create(void (*run)(void *), void *opaque, size_t stack);
void bulk_test_join(sthread_t *thread);
#define malloc bulk_test_malloc
#define free bulk_test_free
#define sthread_create_with_stack_size bulk_test_create
#define sthread_join bulk_test_join
