/* Stable in-place borrowed-pointer compaction, max64 entries. MSVC C89.
 * Typed expansion avoids aliasing a T** array through void**. No pointee writes,
 * allocation or persistent tags. Counts outside [2,64] are left unchanged. */
#ifndef XE_CORE_POINTER_UNIQUE_H
#define XE_CORE_POINTER_UNIQUE_H
#include <stdint.h>
#include <string.h>
#include <retro_inline.h>
static INLINE unsigned xe_pointer_unique_bucket(uintptr_t value)
{
   return (unsigned)(((uint64_t)value * UINT64_C(11400714819323198485)) >> 57);
}
#define XE_POINTER_UNIQUE_DEFINE(NAME, TYPE) \
static INLINE unsigned NAME(TYPE **items, unsigned count) \
{ \
   unsigned char slots[128]; \
   unsigned read, kept = 1, probe, found; \
   TYPE *value; \
   if (count < 2 || count > 64) return count; \
   /* Tiny batches and up to four unique owners need no hash initialization. */ \
   for (read = 1; read < count; ++read) { \
      value = items[read]; \
      if (value == items[kept - 1]) continue; \
      for (found = 0; found < kept && items[found] != value; ++found) {} \
      if (found < kept) continue; \
      if (kept == 4 && count > 8) break; \
      if (kept != read) items[kept] = value; \
      ++kept; \
   } \
   if (read == count) return kept; \
   memset(slots, 0, sizeof(slots)); \
   for (found = 0; found < kept; ++found) { \
      probe = xe_pointer_unique_bucket((uintptr_t)items[found]); \
      while (slots[probe]) probe = (probe + 1) & 127; \
      slots[probe] = (unsigned char)(found + 1); \
   } \
   for (; read < count; ++read) { \
      value = items[read]; \
      if (value == items[kept - 1]) continue; \
      probe = xe_pointer_unique_bucket((uintptr_t)value); \
      while (slots[probe] && items[slots[probe] - 1] != value) \
         probe = (probe + 1) & 127; \
      if (!slots[probe]) { \
         slots[probe] = (unsigned char)(kept + 1); \
         if (kept != read) items[kept] = value; \
         ++kept; \
      } \
   } \
   return kept; \
}
#endif
