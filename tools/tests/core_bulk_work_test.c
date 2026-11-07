/* Actual C89 bulk implementation, with only allocation/thread factories
 * redirected at compile time. No release fault switches. */
#include "libretro/core_bulk_work.h"
#include <rthreads/rthreads.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"bulk work line %u\n",(unsigned)__LINE__); exit(1); } } while (0)
static int fail_allocation, create_budget, created, joined, arrays;
void *bulk_test_malloc(size_t size)
{
   void *p;
   if (fail_allocation) return NULL;
   p = malloc(size);
   if (p) ++arrays;
   return p;
}
void bulk_test_free(void *p)
{
   if (p) --arrays;
   free(p);
}
sthread_t *bulk_test_create(void (*run)(void *), void *opaque, size_t stack)
{
   sthread_t *thread;
   CHECK(stack == 4 * 1024 * 1024);
   if (create_budget == 0) return NULL;
   --create_budget;
   thread = sthread_create_with_stack_size(run, opaque, stack);
   CHECK(thread != NULL);
   ++created;
   return thread;
}
void bulk_test_join(sthread_t *thread)
{
   sthread_join(thread);
   ++joined;
}
#define JOBS 4096
typedef struct fixture {
   xe_bulk_work work;
   retro_atomic_int_t hits[JOBS];
   retro_atomic_int_t callbacks;
   unsigned values[JOBS];
} fixture;
static fixture state;
static void consume(void *opaque)
{
   fixture *f = (fixture *)opaque;
   size_t index, completed = 0;
   retro_atomic_fetch_add_int(&f->callbacks, 1);
   while (xe_bulk_work_claim(&f->work, &index)) {
      CHECK(index < JOBS);
      CHECK(retro_atomic_fetch_add_int(&f->hits[index], 1) == 0);
      f->values[index] = (unsigned)index ^ 0xFEED1234u;
      ++completed;
   }
   xe_bulk_work_complete(&f->work, completed);
}
static void check_case(size_t count, size_t workers, int allocation_fail,
      int budget, int expected_threads)
{
   size_t i;
   CHECK(xe_bulk_work_init(&state.work, count));
   retro_atomic_int_init(&state.callbacks, 0);
   for (i = 0; i < JOBS; ++i) {
      retro_atomic_int_init(&state.hits[i], 0);
      state.values[i] = 0;
   }
   fail_allocation = allocation_fail;
   create_budget = budget;
   created = joined = arrays = 0;
   xe_bulk_work_run(workers, count, consume, &state, "Bulk test");
   CHECK(created == expected_threads && joined == created && arrays == 0);
   CHECK(xe_bulk_work_completed(&state.work) == count);
   CHECK(retro_atomic_load_acquire_int(&state.callbacks) == created + (count != 0));
   CHECK((uint64_t)retro_atomic_load_acquire_64(&state.work.next) == count);
   for (i = 0; i < JOBS; ++i) {
      CHECK(retro_atomic_load_acquire_int(&state.hits[i]) == (i < count));
      if (i < count) CHECK(state.values[i] == ((unsigned)i ^ 0xFEED1234u));
   }
}
int main(void)
{
   size_t i, index = 123;
   CHECK(!xe_bulk_work_init(NULL, 0));
   check_case(0, 8, 0, 8, 0);
   check_case(1, (size_t)-1, 0, 8, 0);
   check_case(17, 0, 0, 0, 0);
   check_case(17, 8, 1, 8, 0);
   check_case(17, 8, 0, 0, 0);
   check_case(JOBS, 8, 0, 2, 2);
   check_case(3, (size_t)-1, 0, 8, 2);
   for (i = 0; i < 25; ++i) check_case(JOBS, 6, 0, 6, 6);
   CHECK(xe_bulk_work_init(&state.work, 17));
   CHECK(xe_bulk_work_fail(&state.work));
   CHECK(!xe_bulk_work_fail(&state.work));
   CHECK(!xe_bulk_work_claim(&state.work, &index) && index == 123);
   /* Last representable claim must not wrap, and exhaustion doesn't advance. */
#if SIZE_MAX > INT64_MAX
   CHECK(!xe_bulk_work_init(&state.work, (size_t)-1));
#endif
#if SIZE_MAX >= INT64_MAX
   {
      CHECK(xe_bulk_work_init(&state.work, (size_t)INT64_MAX));
      retro_atomic_store_release_64(&state.work.next, INT64_MAX - 1);
      CHECK(xe_bulk_work_claim(&state.work, &index));
      CHECK((uint64_t)index == (uint64_t)INT64_MAX - 1);
      CHECK(!xe_bulk_work_claim(&state.work, &index));
   }
#endif
   puts("bulk work C89: exact claims, borrowed writes, zero/single jobs, partial startup and joins PASS");
   return 0;
}
