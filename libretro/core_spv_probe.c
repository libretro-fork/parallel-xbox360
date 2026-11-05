#include "core_spv_probe.h"
size_t xe_spv_compute_probe(uint32_t *words, size_t capacity,
      uint32_t version, uint32_t generator)
{
   size_t count = 0;
   if (!words || capacity < XE_SPV_COMPUTE_PROBE_WORDS ||
       (version & UINT32_C(0xffff00ff)) != UINT32_C(0x00010000) ||
       ((version >> 8) & 255) > 6) return 0;
#define WORD(value) words[count++] = (uint32_t)(value)
   WORD(0x07230203); WORD(version); WORD(generator); WORD(6); WORD(0);
   /* Shader capability, Logical/GLSL450 memory model. */
   WORD(0x00020011); WORD(1);
   WORD(0x0003000e); WORD(0); WORD(1);
   /* GLCompute main, LocalSize 1x1x1, OpName main. */
   WORD(0x0005000f); WORD(5); WORD(3); WORD(0x6e69616d); WORD(0);
   WORD(0x00060010); WORD(3); WORD(17); WORD(1); WORD(1); WORD(1);
   WORD(0x00040005); WORD(3); WORD(0x6e69616d); WORD(0);
   /* void, void(), function, entry label, return, function end. */
   WORD(0x00020013); WORD(1);
   WORD(0x00030021); WORD(2); WORD(1);
   WORD(0x00050036); WORD(1); WORD(3); WORD(0); WORD(2);
   WORD(0x000200f8); WORD(4);
   WORD(0x000100fd); WORD(0x00010038);
#undef WORD
   return count;
}
