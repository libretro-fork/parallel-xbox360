/* Shader reentry write sets. BSD license, MSVC C89.
 * Borrow packed host-endian microcode and per-CF writes. The caller's final
 * result array doubles as interval scratch; no graph, edge or label allocation.
 * All three spans must be disjoint. Results are not published until complete. */
#ifndef XE_CORE_SHADER_REENTRY_H
#define XE_CORE_SHADER_REENTRY_H
#include <stddef.h>
#include <stdint.h>
#include <retro_inline.h>

static INLINE uint64_t xe_shader_reentry_all(const uint64_t *writes,
      size_t count)
{
   size_t i;
   uint64_t result = 0;
   for (i = 0; i < count; ++i) result |= writes[i];
   return result;
}

static INLINE void xe_shader_reentry_edge(uint64_t *out, size_t count,
      size_t source, size_t target)
{
   uint64_t end;
   if (target > source || target >= count) return;
   if (source >= count) source = count - 1;
   end = (uint64_t)source + 1;
   if (out[target] < end) out[target] = end;
}

/* Each backward edge is a closed interval. Overlapping intervals form the
 * same fixed-point span as the old repeated edge scan. Only edge destinations
 * receive the write union. Adjacency alone must not merge two components. */
static INLINE void xe_shader_reentry_finish(const uint64_t *writes,
      uint64_t *out, size_t count)
{
   size_t first, last, i;
   uint64_t mask;
   first = 0;
   while (first < count) {
      if (!out[first]) { ++first; continue; }
      last = (size_t)out[first] - 1;
      mask = 0;
      for (i = first; i <= last; ++i) {
         if (out[i] && (size_t)out[i] - 1 > last)
            last = (size_t)out[i] - 1;
         mask |= writes[i];
      }
      for (i = first; i <= last; ++i)
         if (out[i]) out[i] = mask;
      first = last + 1;
   }
}

/* Replay the analyzer's shrinking CF bound, including the other half of the
 * last pair if an EXEC reduces the bound within that pair. An edge beyond the
 * final bound is clipped, just as the old final write-union scan clipped it.
 * out must initially contain count zeroes (the result owner's initialization).
 * count must be twice the analyzed CF-pair bound. Invalid spans are rejected
 * before access; a mismatched bound returns false with unpublished scratch. */
static INLINE int xe_shader_reentry_analyze(const uint32_t *words,
      size_t word_count, const uint64_t *writes, uint64_t *out, size_t count)
{
   size_t bound, pair, j, source, target;
   uint32_t low, high, opcode;
   const uint32_t *p;
   if ((word_count && !words) || (count && (!writes || !out)) ||
         count > (size_t)-1 / sizeof(*out) ||
         count / 2 > word_count / 3 || (count & 1)) return 0;
   bound = word_count / 3;
   for (pair = 0; pair < bound; ++pair) {
      p = words + pair * 3;
      for (j = 0; j < 2; ++j) {
         low = j ? (p[1] >> 16) | (p[2] << 16) : p[0];
         high = j ? p[2] >> 16 : p[1] & 65535u;
         opcode = high >> 12;
         if ((opcode >= 1 && opcode <= 6) || opcode == 13 || opcode == 14) {
            target = low & 4095u;
            if (target < bound) bound = target;
         }
         if (opcode == 8 || opcode == 11) {
            source = pair * 2 + j;
            target = low & 8191u;
            xe_shader_reentry_edge(out, count, source, target);
         }
      }
   }
   if (bound != count / 2) return 0;
   xe_shader_reentry_finish(writes, out, count);
   return 1;
}
#endif
