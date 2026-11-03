/* Native texture operations. MSVC C89; BSD license, see LICENSE.
 * Guest encodings follow ucode.h, copyright Ben Vanik and contributors. */
#include "guest_texture.h"
#include "guest_resources.h"
#include <dxil_module.h>
#include <dxil_function.h>
int xe_dxil_texture_query_supported(unsigned opcode, unsigned pixel_shader) {
  return opcode == 19 || (opcode == 18 && pixel_shader);
}

static int signed_field(uint32_t value, unsigned shift, unsigned width) {
  unsigned mask = (1u << width) - 1;
  int field = (int)((value >> shift) & mask);
  return field & (1u << (width - 1)) ? field - (int)(1u << width) : field;
}
static XeDxilValue weights_field(XeDxilAlu *s, XeDxilValue word, unsigned shift, unsigned mask) {
  XeDxilValue value = word ? dxil_emit_binop(s->module,DXIL_BINOP_LSHR,word,
    dxil_module_get_int32_const(s->module,(int32_t)shift),0) : NULL;
  return value ? dxil_emit_binop(s->module,DXIL_BINOP_AND,value,
    dxil_module_get_int32_const(s->module,(int32_t)mask),0) : NULL;
}
int xe_dxil_texture_weights(XeDxilAlu *s, const XeDxilTextureInstruction *instruction,
    const XeDxilTextureOperands *operands, XeDxilValue fetch_constants, XeDxilValue result[4]) {
  struct dxil_module *m;
  const struct dxil_func *fraction;
  XeDxilValue packed = NULL, dimension = NULL, size, size_3d, value, args[2];
  unsigned count, i;
  if (!s || !s->module || s->failed || !instruction || !operands || !result ||
      instruction->opcode != 19 || instruction->constant_index > 31 || instruction->dimension > 3 ||
      operands->result_components > 15) return 0;
  m = s->module; count = instruction->dimension == 0 ? 1 : instruction->dimension == 2 ? 3 : 2;
  fraction = dxil_get_function(m,"dx.op.unary",DXIL_F32);
  if (!instruction->unnormalized) {
    if (!fetch_constants) goto fail;
    packed = xe_dxil_cbuffer_word(s,fetch_constants,
      dxil_module_get_int32_const(m,(int32_t)(instruction->constant_index*6+2)));
    if (!packed) goto fail;
    if (instruction->dimension == 2) {
      value = xe_dxil_cbuffer_word(s,fetch_constants,
        dxil_module_get_int32_const(m,(int32_t)(instruction->constant_index*6+5)));
      value = weights_field(s,value,9,3);
      dimension = value ? dxil_emit_cmp(m,DXIL_ICMP_EQ,value,dxil_module_get_int32_const(m,2)) : NULL;
      if (!dimension) goto fail;
    }
  }
  for (i = 0; i < 4; ++i) {
    result[i] = dxil_module_get_float_const(m,0);
    if (i >= count || !(operands->result_components&(1u<<i))) continue;
    value = operands->coordinates[i];
    if (!value || dxil_value_get_type(value) != dxil_module_get_float_type(m,32)) goto fail;
    if (!instruction->unnormalized) {
      size = weights_field(s,packed,instruction->dimension == 0 ? 0 : i*13,
        instruction->dimension == 0 ? 0xffffff : i == 2 ? 63 : 8191);
      if (instruction->dimension == 2) {
        size_3d = weights_field(s,packed,i*11,i == 2 ? 1023 : 2047);
        size = size && size_3d ? dxil_emit_select(m,dimension,size_3d,size) : NULL;
      }
      size = size ? dxil_emit_binop(m,DXIL_BINOP_ADD,size,dxil_module_get_int32_const(m,1),0) : NULL;
      size = size ? dxil_emit_cast(m,DXIL_CAST_UITOFP,dxil_module_get_float_type(m,32),size) : NULL;
      value = size ? dxil_emit_binop(m,DXIL_BINOP_MUL,value,size,0) : NULL;
    }
    value = value ? dxil_emit_binop(m,DXIL_BINOP_ADD,value,dxil_module_get_float_const(m,
      (float)instruction->offset_halves[i]*.5f-.5f+1.5f/1024.0f),0) : NULL;
    args[0] = dxil_module_get_int32_const(m,22); args[1] = value;
    result[i] = fraction && args[0] && value ? dxil_emit_call(m,fraction,args,2) : NULL;
    if (!result[i]) goto fail;
  }
  return 1;
fail:
  s->failed = 1; return 0;
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

static int texture_begin(XeDxilAlu *s, const XeDxilAluIo *io, unsigned block) {
  struct dxil_func_def *function = s->module->cur_emitting_func;
  if (block >= function->num_basic_block_ids || function->basic_block_ids[block] != -1) return 0;
  function->basic_block_ids[block] = (int)function->curr_block;
  *io->current_block = block;
  return 1;
}

static XeDxilValue texture_storage(XeDxilAlu *s) {
  XeDxilValue pointer;
  pointer = dxil_emit_alloca(s->module, dxil_module_get_float_type(s->module, 32),
    dxil_module_get_int32_const(s->module, 1), 4);
  if (!pointer || !dxil_emit_store(s->module, dxil_module_get_float_const(s->module, 0.0f),
      pointer, 4, 0)) return NULL;
  return pointer;
}

int xe_dxil_texture_fetch_init(XeDxilAlu *s, XeDxilTextureFetch *fetch) {
  unsigned i;
  if (!s || !s->module || s->failed || !fetch) return 0;
  if (!s->module->cur_emitting_func || s->module->cur_emitting_func->curr_block) goto fail;
  fetch->lod_storage = texture_storage(s);
  if (!fetch->lod_storage) goto fail;
  for (i = 0; i < 3; ++i) {
    fetch->ddx_storage[i] = texture_storage(s);
    fetch->ddy_storage[i] = texture_storage(s);
    if (!fetch->ddx_storage[i] || !fetch->ddy_storage[i]) goto fail;
  }
  return 1;
fail:
  s->failed = 1;
  return 0;
}

int xe_dxil_emit_texture_fetch(XeDxilAlu *s, const XeDxilTextureInstruction *instruction,
                                const XeDxilAluIo *io, XeDxilTextureFetch *fetch) {
  XeDxilTextureOperands operands;
  XeDxilValue result[4], values[4], offset, condition, args[2], pointer;
  const struct dxil_func *function;
  unsigned i, swizzle, mask = 0, used = 0, blocks = 0, setter, count;
  if (!s || !s->module || s->failed || !instruction || !io || !fetch) return 0;
  setter = instruction->opcode >= 24 && instruction->opcode <= 26;
  if (!s->module->cur_emitting_func || !io->current_block || instruction->source > 63 ||
      instruction->destination > 63 || instruction->source_swizzle > 63 ||
      instruction->destination_swizzle > 4095 || instruction->dimension > 3 ||
      (!setter && instruction->opcode != 1 && instruction->opcode != 16 &&
       instruction->opcode != 17 && instruction->opcode != 18 && instruction->opcode != 19)) goto fail;
  if (!setter) for (i = 0; i < 4; ++i) {
    swizzle = (instruction->destination_swizzle >> (i * 3)) & 7;
    if (swizzle == 6) goto fail;
    if (swizzle != 7) mask |= 1u << i;
    if (swizzle < 4) used |= 1u << swizzle;
  }
  if (!setter && !mask) return 1;
  if ((!setter && !io->write) || ((setter || used) && (!io->read ||
      (instruction->source_relative && !io->loop_address))) ||
      (!setter && instruction->destination_relative && !io->loop_address)) goto fail;
  if (instruction->predicated) {
    condition = s->predicate;
    if (!condition) goto fail;
    if (!instruction->predicate_condition) condition = dxil_emit_binop(s->module, DXIL_BINOP_XOR,
      condition, dxil_module_get_int1_const(s->module, 1), 0);
    if (!condition || !dxil_function_reserve_blocks(s->module, 2, &blocks) ||
        !dxil_emit_branch(s->module, condition, blocks, blocks + 1) ||
        !texture_begin(s, io, blocks)) goto fail;
  }
  memset(&operands, 0, sizeof(operands));
  for (i = 0; i < 4; ++i) result[i] = NULL;
  if (setter || used) {
    count = instruction->dimension == 0 ? 1 : instruction->dimension == 1 ? 2 : 3;
    if (instruction->opcode == 24) count = 1;
    else if (instruction->opcode == 25 || instruction->opcode == 26) count = 3;
    else if (instruction->opcode == 18) count = 2;
    else if (instruction->opcode == 1 && !instruction->dimension &&
        (((instruction->source_swizzle >> 2) & 3) != (instruction->source_swizzle & 3) ||
         ((instruction->source_swizzle >> 4) & 3) != (instruction->source_swizzle & 3))) count = 2;
    operands.coordinate_count = count; operands.result_components = used;
    offset = instruction->source_relative ? io->loop_address : NULL;
    for (i = 0; i < count; ++i) {
      operands.coordinates[i] = io->read(io->context, 0, instruction->source,
        (instruction->source_swizzle >> (i * 2)) & 3, offset);
      if (!is_float(s, operands.coordinates[i])) goto fail;
    }
    if (setter) {
      for (i = 0; i < count; ++i) {
        pointer = instruction->opcode == 24 ? fetch->lod_storage :
          instruction->opcode == 25 ? fetch->ddx_storage[i] : fetch->ddy_storage[i];
        if (!pointer || !dxil_emit_store(s->module, operands.coordinates[i], pointer, 4, 0)) goto fail;
      }
    } else if (instruction->opcode == 18) {
      if (s->module->shader_kind != DXIL_PIXEL_SHADER) goto fail;
      function = dxil_get_function(s->module, "dx.op.unary", DXIL_F32);
      if (!function) goto fail;
      /* {ddx(src.x), ddy(src.x), ddx(src.y), ddy(src.y)}. */
      for (i = 0; i < 4; ++i) if (used & (1u << i)) {
        args[0] = dxil_module_get_int32_const(s->module, (i & 1) ? 84 : 83);
        args[1] = operands.coordinates[i >> 1];
        result[i] = dxil_emit_call(s->module, function, args, 2);
        if (!is_float(s, result[i])) goto fail;
      }
    } else {
      if (!fetch->fetch || !fetch->lod_storage) goto fail;
      operands.lod = dxil_emit_load(s->module, fetch->lod_storage, 4, 0);
      if (!operands.lod) goto fail;
      for (i = 0; i < 3; ++i) {
        if (!fetch->ddx_storage[i] || !fetch->ddy_storage[i]) goto fail;
        operands.ddx[i] = dxil_emit_load(s->module, fetch->ddx_storage[i], 4, 0);
        operands.ddy[i] = dxil_emit_load(s->module, fetch->ddy_storage[i], 4, 0);
        if (!operands.ddx[i] || !operands.ddy[i]) goto fail;
      }
      if (!fetch->fetch(fetch->context, instruction, &operands, result)) goto fail;
    }
  }
  if (!setter) {
    for (i = 0; i < 4; ++i) {
      swizzle = (instruction->destination_swizzle >> (i * 3)) & 7;
      values[i] = swizzle < 4 ? result[swizzle] : swizzle == 7 ? NULL :
        dxil_module_get_float_const(s->module, swizzle == 5 ? 1.0f : 0.0f);
      if ((mask & (1u << i)) && !is_float(s, values[i])) goto fail;
    }
    offset = instruction->destination_relative ? io->loop_address : NULL;
    if (!io->write(io->context, 0, instruction->destination, mask, values, offset)) goto fail;
  }
  if (instruction->predicated && (!dxil_emit_branch(s->module, NULL, blocks + 1, 0) ||
      !texture_begin(s, io, blocks + 1))) goto fail;
  return !s->failed;
fail:
  s->failed = 1;
  return 0;
}
