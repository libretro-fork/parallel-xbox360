#include <stdlib.h>
#include <stdio.h>
static size_t allocations, frees;
static int denied;
static void *plan_alloc(size_t bytes)
{
   if (denied) return NULL;
   ++allocations;
   return malloc(bytes);
}
static void plan_free(void *pointer)
{
   if (pointer) ++frees;
   free(pointer);
}
#define XE_SPV_PLAN_ALLOC plan_alloc
#define XE_SPV_PLAN_FREE plan_free
#include "../../libretro/core_spv_plan.h"
int main(void)
{
   xe_spv_plan plan = {0};
   size_t i, pass, warm;
   const void *node, **stable;
   int values[1024];
   for (i = 0; i < 64; ++i)
      if (!xe_spv_plan_push(&plan, &values[i])) return 1;
   if (allocations) return 2;
   denied = 1;
   if (xe_spv_plan_push(&plan, NULL) || plan.count != 64 || plan.heap) return 3;
   denied = 0;
   for (i = 64; i < 1024; ++i)
      if (!xe_spv_plan_push(&plan, &values[i])) return 4;
   if (!xe_spv_plan_push(&plan, NULL)) return 5;
   for (i = 0; i < 1024; ++i)
      if (!xe_spv_plan_next(&plan, &node) || node != &values[i]) return 6;
   if (!xe_spv_plan_next(&plan, &node) || node || xe_spv_plan_next(&plan, &node)) return 7;
   warm = allocations;
   stable = plan.heap;
   denied = 1;
   for (pass = 0; pass < 10000; ++pass) {
      xe_spv_plan_reset(&plan);
      for (i = 0; i < 1024; ++i)
         if (!xe_spv_plan_push(&plan, &values[i])) return 8;
      if (!xe_spv_plan_push(&plan, NULL) || allocations != warm || plan.heap != stable) return 9;
   }
   xe_spv_plan_reset(&plan);
   if (plan.cursor || plan.count || xe_spv_plan_next(&plan, &node)) return 10;
   xe_spv_plan_close(&plan);
   xe_spv_plan_close(&plan);
   if (allocations != frees) return 11;
   puts("PASS: borrowed emission order, inline storage, growth failure and 10000 allocation-free warm reuses");
   return 0;
}
