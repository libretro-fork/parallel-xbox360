/* Negative ASan control: cached bytes must be inaccessible after close. C89. */
#include "libretro/core_shader_ucode_reuse.h"
int main(void)
{
 xe_shader_ucode_reuse owner={0};volatile uint32_t *borrow;uint32_t value;
 if(!xe_shader_ucode_reuse_allocate(&owner,1024))return 2;
 borrow=owner.words;borrow[0]=123;
 xe_shader_ucode_reuse_close(&owner);
 value=borrow[0];
 xe_shader_ucode_reuse_clear();
 return value==123?0:3;
}
