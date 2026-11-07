#include "libretro/core_state_budget.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do {if(!(x)) {fprintf(stderr,"budget line %u\n",(unsigned)__LINE__);exit(1);}} while(0)
int main(void) {
 xe_state_budget budget={7,8};
 CHECK(!xe_state_budget_begin(&budget,0,-1) && budget.start==7 && budget.limit==8);
 CHECK(!xe_state_budget_begin(NULL,0,1));
 CHECK(xe_state_budget_begin(&budget,100,1000));
 CHECK(xe_state_budget_left(&budget,100)==1000);
 CHECK(xe_state_budget_left(&budget,400)==700);
 CHECK(xe_state_budget_left(&budget,900)==200);
 CHECK(!xe_state_budget_left(&budget,1100) && !xe_state_budget_left(&budget,99));
 CHECK(xe_state_budget_begin(&budget,INT64_MIN,INT64_MAX));
 CHECK(xe_state_budget_left(&budget,INT64_MIN)==INT64_MAX);
 CHECK(!xe_state_budget_left(&budget,INT64_MAX));
 CHECK(xe_state_budget_begin(&budget,INT64_MAX,0));
 CHECK(!xe_state_budget_left(&budget,INT64_MAX));
 puts("PASS: one timeout budget, zero/negative bounds, backwards clocks and full int64 range");return 0;
}
