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

#ifdef __cplusplus
}
#endif
#endif
