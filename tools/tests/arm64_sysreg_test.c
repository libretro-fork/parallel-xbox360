/* ARM64 register-read runtime check. MSVC C89-compatible; BSD license. */
#include "xenia/base/arm64_sysreg.h"
#include <stdio.h>
int main(void) {
  uint64_t frequency, before, after, zero;
  frequency = xe_arm64_read_cntfrq_el0();
  before = xe_arm64_read_cntvct_el0();
  zero = xe_arm64_read_dczid_el0();
  after = xe_arm64_read_cntvct_el0();
  (void)zero;
  if (!frequency || after < before) return 1;
  puts("ARM64 system registers: nonzero frequency and monotonic counter matched.");
  return 0;
}
