/* Native texture operations. MSVC C89; BSD license, see LICENSE.
 * Guest encodings follow ucode.h, copyright Ben Vanik and contributors. */
#include "guest_texture.h"
#include <dxil_module.h>
#include <dxil_function.h>

static int signed_field(uint32_t value, unsigned shift, unsigned width) {
  unsigned mask = (1u << width) - 1;
  int field = (int)((value >> shift) & mask);
  return field & (1u << (width - 1)) ? field - (int)(1u << width) : field;
}

int xe_dxil_decode_texture(const uint32_t *words, size_t count, XeDxilTextureInstruction *out) {
  unsigned opcode, i;
  if (!words || !out || count < 3) return 0;
  opcode = words[0] & 31;
  if (opcode != 1 && opcode != 16 && opcode != 17 && opcode != 18 && opcode != 19 &&
      opcode != 24 && opcode != 25 && opcode != 26) return 0;
  memset(out, 0, sizeof(*out)); out->opcode = opcode;
  out->source = (words[0] >> 5) & 63; out->source_relative = (words[0] >> 11) & 1;
  out->destination = (words[0] >> 12) & 63; out->destination_relative = (words[0] >> 18) & 1;
  out->valid_only = (words[0] >> 19) & 1; out->constant_index = (words[0] >> 20) & 31;
  out->unnormalized = (words[0] >> 25) & 1; out->source_swizzle = words[0] >> 26;
  out->destination_swizzle = words[1] & 4095;
  out->mag_filter = (words[1] >> 12) & 3; out->min_filter = (words[1] >> 14) & 3;
  out->mip_filter = (words[1] >> 16) & 3; out->aniso_filter = (words[1] >> 18) & 7;
  out->arbitrary_filter = (words[1] >> 21) & 7;
  out->volume_mag_filter = (words[1] >> 24) & 3; out->volume_min_filter = (words[1] >> 26) & 3;
  out->computed_lod = (words[1] >> 28) & 1; out->register_lod = (words[1] >> 29) & 1;
  out->predicated = words[1] >> 31; out->register_gradients = words[2] & 1;
  out->sample_location = (words[2] >> 1) & 1;
  out->lod_bias_sixteenths = signed_field(words[2], 2, 7);
  out->dimension = (words[2] >> 14) & 3;
  for (i = 0; i < 3; ++i) out->offset_halves[i] = signed_field(words[2], 16 + i * 5, 5);
  out->predicate_condition = words[2] >> 31;
  return 1;
}

static int is_float(XeDxilAlu *s, XeDxilValue value) {
  return value && dxil_value_get_type(value) == dxil_module_get_float_type(s->module, 32);
}

int xe_dxil_texture_sample(XeDxilAlu *s, const XeDxilTextureSample *sample, XeDxilValue values[4]) {
  static const char *names[4] = {"dx.op.sampleLevel", "dx.op.sampleGrad", "dx.op.sample", "dx.op.sampleBias"};
  static const unsigned opcodes[4] = {62,63,60,61};
  XeDxilValue args[17], result, undef_float, undef_int;
  const struct dxil_func *function;
  unsigned coordinates, gradients, offsets, count, i;
  if (!s || !s->module || s->failed || !sample || !values) return 0;
  if (!s->module->cur_emitting_func || !sample->texture || !sample->sampler || sample->mode > 3 ||
      sample->dimension > XE_DXIL_SAMPLE_2D_ARRAY) goto fail;
  if (sample->mode >= XE_DXIL_SAMPLE_IMPLICIT && s->module->shader_kind != DXIL_PIXEL_SHADER) goto fail;
  coordinates = sample->dimension == XE_DXIL_SAMPLE_1D ? 1 : sample->dimension == XE_DXIL_SAMPLE_2D ? 2 : 3;
  gradients = sample->dimension == XE_DXIL_SAMPLE_2D_ARRAY ? 2 : coordinates;
  offsets = sample->dimension == XE_DXIL_SAMPLE_CUBE ? 0 : gradients;
  for (i = 0; i < coordinates; ++i) if (!is_float(s, sample->coordinates[i])) goto fail;
  for (i = 0; i < 3; ++i) {
    if (sample->offsets[i] < -8 || sample->offsets[i] > 7 || (i >= offsets && sample->offsets[i])) goto fail;
    if (sample->mode == XE_DXIL_SAMPLE_GRAD && i < gradients &&
        (!is_float(s, sample->ddx[i]) || !is_float(s, sample->ddy[i]))) goto fail;
  }
  if ((sample->mode == XE_DXIL_SAMPLE_LEVEL || sample->mode == XE_DXIL_SAMPLE_BIAS) &&
      !is_float(s, sample->lod_or_bias)) goto fail;
  if (sample->mode != XE_DXIL_SAMPLE_LEVEL && sample->lod_clamp && !is_float(s, sample->lod_clamp)) goto fail;
  undef_float = dxil_module_get_undef(s->module, dxil_module_get_float_type(s->module, 32));
  undef_int = dxil_module_get_undef(s->module, dxil_module_get_int_type(s->module, 32));
  if (!undef_float || !undef_int) goto fail;
  for (i = 0; i < 17; ++i) args[i] = NULL;
  args[0] = dxil_module_get_int32_const(s->module, (int32_t)opcodes[sample->mode]);
  args[1] = sample->texture; args[2] = sample->sampler;
  for (i = 0; i < 4; ++i) args[3 + i] = i < coordinates ? sample->coordinates[i] : undef_float;
  for (i = 0; i < 3; ++i) args[7 + i] = i < offsets ?
    dxil_module_get_int32_const(s->module, sample->offsets[i]) : undef_int;
  count = 11;
  args[10] = sample->lod_clamp ? sample->lod_clamp : dxil_module_get_float_const(s->module, 0.0f);
  if (sample->mode == XE_DXIL_SAMPLE_LEVEL) args[10] = sample->lod_or_bias;
  else if (sample->mode == XE_DXIL_SAMPLE_BIAS) { args[11] = args[10]; args[10] = sample->lod_or_bias; count = 12; }
  else if (sample->mode == XE_DXIL_SAMPLE_GRAD) {
    args[16] = args[10]; count = 17;
    for (i = 0; i < 3; ++i) {
      args[10 + i] = i < gradients ? sample->ddx[i] : undef_float;
      args[13 + i] = i < gradients ? sample->ddy[i] : undef_float;
    }
  }
  for (i = 0; i < count; ++i) if (!args[i]) goto fail;
  function = dxil_get_function(s->module, names[sample->mode], DXIL_F32);
  result = function ? dxil_emit_call(s->module, function, args, count) : NULL;
  if (!result) goto fail;
  for (i = 0; i < 4; ++i) {
    values[i] = dxil_emit_extractval(s->module, result, i);
    if (!values[i]) goto fail;
  }
  return 1;
fail:
  s->failed = 1;
  return 0;
}
