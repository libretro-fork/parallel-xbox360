/* Core-owned dynamic DXIL block IDs. MSVC C89; BSD, see LICENSE. */
#include <dxil_module.h>

bool dxil_function_reserve_blocks(struct dxil_module *m, unsigned count,
                                  unsigned *first_id)
{
   struct dxil_func_def *function;
   int *replacement;
   size_t required;
   size_t capacity;
   size_t i;
   if (!m || !first_id || !count || !m->cur_emitting_func)
      return false;
   function = m->cur_emitting_func;
   if (count > INT_MAX || function->num_basic_block_ids > (size_t)INT_MAX - count)
      return false;
   required = function->num_basic_block_ids + count;
   if (required > function->basic_block_capacity) {
      capacity = function->basic_block_capacity;
      if (capacity < 64) capacity = 64;
      while (capacity < required) {
         if (capacity > (size_t)INT_MAX / 2) { capacity = required; break; }
         capacity *= 2;
      }
      replacement = reralloc(m->ralloc_ctx, function->basic_block_ids, int, capacity);
      if (!replacement) return false;
      function->basic_block_ids = replacement;
      function->basic_block_capacity = capacity;
   }
   for (i = function->num_basic_block_ids; i < required; ++i)
      function->basic_block_ids[i] = -1;
   *first_id = (unsigned)function->num_basic_block_ids;
   function->num_basic_block_ids = required;
   return true;
}
