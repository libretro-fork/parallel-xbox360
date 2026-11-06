/* C89: bounded reservation, untouched inactive lanes and overflow canaries. */
#include "libretro/core_spv_export_cases.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "case test line %u\n", (unsigned)__LINE__); abort(); } } while (0)
int main(void) {
  struct {
    uint32_t before;
    xe_spv_export_cases cases;
    uint32_t after;
  } guarded;
  xe_spv_export_case *slot;
  unsigned mask, index, lane;
  for (mask = 0; mask < 32; ++mask) {
    memset(&guarded, 0xA5, sizeof(guarded));
    guarded.cases.count = 0;
    CHECK(!xe_spv_export_case_begin(&guarded.cases, 5));
    CHECK(guarded.cases.count == 0);
    for (index = 0; index < XE_SPV_EXPORT_FORMAT_CASES; ++index) {
      slot = xe_spv_export_case_begin(&guarded.cases, index % 5);
      CHECK(slot == &guarded.cases.data[index]);
      CHECK(slot->element_bytes_log2 == index % 5 && slot->phi_parent == 0);
      for (lane = 0; lane < XE_SPV_EXPORT_REGISTERS; ++lane) {
        CHECK(slot->packed[lane] == UINT32_C(0xA5A5A5A5));
        if (mask & (1u << lane)) slot->packed[lane] = 1 + index * 5 + lane;
      }
      slot->phi_parent = index + 100;
    }
    CHECK(!xe_spv_export_case_begin(&guarded.cases, 0));
    CHECK(guarded.cases.count == XE_SPV_EXPORT_FORMAT_CASES);
    for (index = 0; index < XE_SPV_EXPORT_FORMAT_CASES; ++index) {
      slot = &guarded.cases.data[index];
      CHECK(slot->phi_parent == index + 100);
      for (lane = 0; lane < XE_SPV_EXPORT_REGISTERS; ++lane)
        CHECK(slot->packed[lane] == ((mask & (1u << lane)) ?
            1 + index * 5 + lane : UINT32_C(0xA5A5A5A5)));
    }
    guarded.cases.count = UINT32_MAX;
    CHECK(!xe_spv_export_case_begin(&guarded.cases, 0));
    CHECK(guarded.cases.count == UINT32_MAX);
    CHECK(guarded.before == UINT32_C(0xA5A5A5A5));
    CHECK(guarded.after == UINT32_C(0xA5A5A5A5));
  }
  puts("PASS: 19 bounded export cases, all register masks, rejected overflow and untouched inactive lanes");
  return 0;
}
