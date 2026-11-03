/* Raw guest texture decoding and native sampling. MSVC C89; BSD license. */
#ifndef XE_GPU_DXIL_GUEST_TEXTURE_H
#define XE_GPU_DXIL_GUEST_TEXTURE_H
#include "guest_alu_instruction.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct XeDxilTextureInstruction {
  unsigned opcode, source, source_relative, destination, destination_relative;
  unsigned source_swizzle, destination_swizzle, constant_index, dimension;
  unsigned predicated, predicate_condition, valid_only, unnormalized;
  unsigned mag_filter, min_filter, mip_filter, aniso_filter, arbitrary_filter;
  unsigned volume_mag_filter, volume_min_filter;
  unsigned computed_lod, register_lod, register_gradients, sample_location;
  /* Signed fixed-point fields: bias in sixteenths, offsets in halves. */
  int lod_bias_sixteenths, offset_halves[3];
} XeDxilTextureInstruction;

int xe_dxil_decode_texture(const uint32_t *words, size_t count,
                            XeDxilTextureInstruction *instruction);
/* Cheap renderer preflight for descriptor-free queries with used results. */
int xe_dxil_texture_query_supported(unsigned opcode, unsigned pixel_shader);

enum XeDxilSampleMode {
  XE_DXIL_SAMPLE_LEVEL = 0, XE_DXIL_SAMPLE_GRAD = 1,
  XE_DXIL_SAMPLE_IMPLICIT = 2, XE_DXIL_SAMPLE_BIAS = 3
};
enum XeDxilSampleDimension {
  XE_DXIL_SAMPLE_1D = 0, XE_DXIL_SAMPLE_2D = 1, XE_DXIL_SAMPLE_3D = 2,
  XE_DXIL_SAMPLE_CUBE = 3, XE_DXIL_SAMPLE_2D_ARRAY = 4
};

typedef struct XeDxilTextureSample {
  XeDxilValue texture, sampler;
  unsigned mode, dimension;
  XeDxilValue coordinates[4];
  XeDxilValue ddx[3], ddy[3];
  XeDxilValue lod_or_bias;
  XeDxilValue lod_clamp; /* NULL means zero; unused for explicit level. */
  int offsets[3]; /* Native integer texel offsets, each -8..7. */
} XeDxilTextureSample;

/* Emit a native float sample from declared texture/sampler handles. Implicit
 * derivatives and bias require a pixel shader. Coordinates are normalized;
 * guest half-texel offsets, cube face conversion, fetch-constant policies,
 * signed/gamma adjustments and sampler selection belong to the stage adapter.
 * This primitive neither changes ALU state nor writes guest registers. */
int xe_dxil_texture_sample(XeDxilAlu *alu, const XeDxilTextureSample *sample,
                            XeDxilValue values[4]);

/* Persistent guest state survives control-flow edges, like full/mini vertex
 * fetch state. The adapter applies texture format/coordinate/sampler policies;
 * these values have already passed guest register addressing and swizzling.
 * Sample/query callbacks may add blocks and update io->current_block. */
typedef struct XeDxilTextureOperands {
  XeDxilValue coordinates[3];
  unsigned coordinate_count, result_components;
  XeDxilValue lod, ddx[3], ddy[3];
} XeDxilTextureOperands;
/* Base-level filtering weights, matching the core's guest query policy.
 * Cube queries return ST weights, unused lanes (including mip weight) are
 * zero. Dimensions come directly from the raw six-word fetch constant. */
int xe_dxil_texture_weights(XeDxilAlu *alu, const XeDxilTextureInstruction *instruction,
  const XeDxilTextureOperands *operands, XeDxilValue fetch_constants, XeDxilValue result[4]);

typedef struct XeDxilTextureFetch {
  void *context;
  XeDxilValue lod_storage, ddx_storage[3], ddy_storage[3];
  int (*fetch)(void *context, const XeDxilTextureInstruction *instruction,
                 const XeDxilTextureOperands *operands, XeDxilValue result[4]);
} XeDxilTextureFetch;

/* Initialize once in the entry block, before guest control flow. */
int xe_dxil_texture_fetch_init(XeDxilAlu *alu, XeDxilTextureFetch *fetch);
/* Setters update persistent state without touching ALU p0/ps/a0. Fetches and
 * queries write only requested destination lanes. False predication suppresses
 * source reads, state changes and sampling. getGradients uses native pixel
 * derivatives; other queries are delegated with their raw opcode intact. */
int xe_dxil_emit_texture_fetch(XeDxilAlu *alu,
                                const XeDxilTextureInstruction *instruction,
                                const XeDxilAluIo *io, XeDxilTextureFetch *fetch);

#ifdef __cplusplus
}
#endif
#endif
