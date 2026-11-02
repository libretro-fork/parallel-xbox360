/* Native vertex fetch conversion. MSVC C89; BSD license, see LICENSE. */
#ifndef XE_GPU_DXIL_GUEST_VERTEX_H
#define XE_GPU_DXIL_GUEST_VERTEX_H
#include "guest_alu_instruction.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct XeDxilVertexInstruction {
  unsigned source, source_component, source_relative;
  unsigned destination, destination_relative, swizzle;
  unsigned constant_index, stride, format;
  unsigned signed_value, integer_value, signed_no_zero, rounded_index;
  unsigned mini, predicated, predicate_condition;
  int exponent_adjust, offset;
} XeDxilVertexInstruction;

int xe_dxil_decode_vertex(const uint32_t *words, size_t count,
                           XeDxilVertexInstruction *instruction);
/* Returns a bitmask of source dwords required for destination swizzle lanes.
 * Invalid formats return -1. Components beyond the format become zero. */
int xe_dxil_vertex_needed_words(unsigned format, unsigned swizzle);
/* Convert already loaded, guest-endian dwords to scalar float values.
 * Only required dwords need non-NULL values. Endian is a runtime i32 0..3.
 * Integer normalization follows the guest translator; half floats currently
 * use IEEE binary16, as does the Vulkan path (not extended-range Xenos f16).
 * Swizzle 4/5 writes zero/one; 7 preserves the lane; reserved 6 is rejected. */
int xe_dxil_vertex_convert(XeDxilAlu *alu,
                            const XeDxilVertexInstruction *instruction,
                            XeDxilValue words[4], XeDxilValue endian,
                            XeDxilValue values[4], unsigned *write_mask);

typedef struct XeDxilVertexFetch {
  void *context;
  XeDxilValue (*constant)(void *context, unsigned index, unsigned word);
  /* Returns a raw i32. in_bounds is i1; the callback must make out-of-range
   * memory access safe, returning zero. It may create continuation blocks and
   * update io->current_block, like the kill callback. Addresses are dwords. */
  XeDxilValue (*load)(void *context, XeDxilValue address, XeDxilValue in_bounds);
  XeDxilValue address_storage, bound_storage, endian_storage;
} XeDxilVertexFetch;

/* Create persistent full/mini-fetch state in the shader entry block. */
int xe_dxil_vertex_fetch_init(XeDxilAlu *alu, XeDxilVertexFetch *fetch);
/* Full fetch stores address/bound/byte order even when no lanes are loaded.
 * Mini fetch reuses that runtime state. Predication suppresses both state
 * changes and memory access. No ALU previous-scalar/predicate state is changed. */
int xe_dxil_emit_vertex_fetch(XeDxilAlu *alu,
                               const XeDxilVertexInstruction *instruction,
                               const XeDxilAluIo *io, XeDxilVertexFetch *fetch);

#ifdef __cplusplus
}
#endif
#endif
