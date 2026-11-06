/* C++ compiler's old bitfield ABI, without a core/frontend/backend launch. */
#include <cstdio>
#include <cstring>
#include "core_instruction_flags.h"
struct OldFlags {
  uint32_t was_resolved:1, accessed_mmio:1, is_syscall_func:1, is_return_site:1, reserved:28;
};
static_assert(sizeof(OldFlags)==4 && sizeof(xe_ic_flags)==4);
int main() {
  const uint32_t masks[]={XE_IC_FLAG_RESOLVED,XE_IC_FLAG_MMIO,XE_IC_FLAG_SYSCALL,XE_IC_FLAG_RETURN_SITE};
  for(unsigned i=0;i<4;++i) {
    OldFlags old{};
    switch(i) {
      case 0:old.was_resolved=1;break;
      case 1:old.accessed_mmio=1;break;
      case 2:old.is_syscall_func=1;break;
      default:old.is_return_site=1;break;
    }
    uint32_t raw=0; std::memcpy(&raw,&old,4);
    if(raw!=masks[i]) return 1;
  }
  std::puts("C++ legacy instruction-bit ABI matches version-4 word masks");
  return 0;
}
