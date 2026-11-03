/* Guest texture fetch policies, without a shader-language intermediary.
 * MSVC C89; BSD license. Rules follow the owned Xenia texture translator. */
#include "guest_texture_stage.h"
#include "guest_resources.h"
#include "edram_format.h"
#include <dxil_module.h>
#include <dxil_function.h>
#include <stdlib.h>
#include <string.h>
static unsigned computed_lod(const XeDxilTextureInstruction *i, unsigned pixel) { return i->computed_lod && (pixel || i->register_gradients); }
static unsigned aniso(const XeDxilTextureInstruction *i, unsigned pixel) {
  unsigned a = computed_lod(i,pixel) ? i->aniso_filter : 0;
  return a == 7 ? a : a > 5 ? 5 : a;
}
static int layout_valid(const XeDxilTextureLayout *l) {
  return l && l->texture_count <= XE_DXIL_TEXTURE_BINDINGS_MAX && l->sampler_count <= XE_DXIL_SAMPLER_BINDINGS_MAX;
}
int xe_dxil_texture_layout_texture(const XeDxilTextureLayout *l, unsigned fc, unsigned dimension, unsigned sign) {
  unsigned n;
  if (!layout_valid(l)) return -1;
  for (n = 0; n < l->texture_count; ++n) if (l->textures[n].fetch_constant == fc && l->textures[n].dimension == dimension && l->textures[n].is_signed == sign) return (int)n;
  return -1;
}
int xe_dxil_texture_layout_sampler(const XeDxilTextureLayout *l, const XeDxilTextureInstruction *i, unsigned pixel) {
  unsigned n, a;
  if (!layout_valid(l) || !i) return -1;
  a = aniso(i,pixel);
  for (n = 0; n < l->sampler_count; ++n) if (l->samplers[n].fetch_constant == i->constant_index &&
      l->samplers[n].mag_filter == i->mag_filter && l->samplers[n].min_filter == i->min_filter &&
      l->samplers[n].mip_filter == (i->opcode == 17 ? 1u : i->mip_filter) && l->samplers[n].aniso_filter == a) return (int)n;
  return -1;
}
int xe_dxil_texture_layout_add(XeDxilTextureLayout *l, const XeDxilTextureInstruction *i, unsigned pixel) {
  unsigned sign, dimension, first, last, needed = 0; XeDxilSamplerBinding *s; XeDxilTextureBinding *t; int sampler;
  if (!layout_valid(l) || !i || i->constant_index > 31) return 0;
  if (xe_dxil_texture_query_supported(i->opcode,pixel)) return 1;
  if (i->opcode == 17 && (!pixel || !i->computed_lod || i->register_gradients)) return 1;
  if ((i->opcode != 1 && i->opcode != 17) || i->dimension > 3 || i->mag_filter > 3 || i->min_filter > 3 || i->mip_filter > 3 || i->aniso_filter > 7) return 0;
  first = i->dimension == 3 ? 3 : 1; last = i->dimension == 2 ? 2 : first;
  for (dimension = first; dimension <= last; ++dimension) for (sign = 0; sign < 2; ++sign) if (xe_dxil_texture_layout_texture(l,i->constant_index,dimension,sign) < 0) ++needed;

  sampler = xe_dxil_texture_layout_sampler(l,i,pixel);
  if (needed > XE_DXIL_TEXTURE_BINDINGS_MAX-l->texture_count || (sampler < 0 && l->sampler_count == XE_DXIL_SAMPLER_BINDINGS_MAX)) return 0;
  for (dimension = first; dimension <= last; ++dimension) for (sign = 0; sign < 2; ++sign) if (xe_dxil_texture_layout_texture(l,i->constant_index,dimension,sign) < 0) {
    t = &l->textures[l->texture_count++]; t->fetch_constant = i->constant_index; t->dimension = dimension; t->is_signed = sign;
  }
  if (sampler < 0) { s = &l->samplers[l->sampler_count++]; s->fetch_constant = i->constant_index;
    s->mag_filter = i->mag_filter; s->min_filter = i->min_filter; s->mip_filter = i->opcode == 17 ? 1 : i->mip_filter; s->aniso_filter = aniso(i,pixel); }
  l->used_texture_mask |= 1u<<i->constant_index;
  if (i->opcode == 1) l->sampled_texture_mask |= 1u<<i->constant_index;
  return 1;
}
struct XeDxilTextureStage {
  XeDxilShaderModule *shader;
  XeDxilAlu alu;
  XeDxilTextureStageOptions options;
  XeDxilValue system, fetch, indices, signs[32], integer_bits[32], constants[32][4];
  unsigned texture_resource[4], sampler_resource, current_block;
};
typedef struct XeDxilTextureStage TextureStage;
static XeDxilValue ic(TextureStage *t, uint32_t v) { return dxil_module_get_int32_const(t->alu.module,(int32_t)v); }
static XeDxilValue fc(TextureStage *t, float v) { return dxil_module_get_float_const(t->alu.module,v); }
static XeDxilValue bin(TextureStage *t, enum dxil_bin_opcode op, XeDxilValue a, XeDxilValue b) {
  XeDxilValue v = a && b ? dxil_emit_binop(t->alu.module,op,a,b,0) : NULL; if (!v) t->alu.failed = 1; return v;
}
static XeDxilValue cmp(TextureStage *t, enum dxil_cmp_pred op, XeDxilValue a, XeDxilValue b) {
  XeDxilValue v = a && b ? dxil_emit_cmp(t->alu.module,op,a,b) : NULL; if (!v) t->alu.failed = 1; return v;
}
static XeDxilValue pick(TextureStage *t, XeDxilValue c, XeDxilValue a, XeDxilValue b) {
  XeDxilValue v = c && a && b ? dxil_emit_select(t->alu.module,c,a,b) : NULL; if (!v) t->alu.failed = 1; return v;
}
static XeDxilValue cast(TextureStage *t, enum dxil_cast_opcode op, unsigned floating, XeDxilValue v) {
  v = v ? dxil_emit_cast(t->alu.module,op,floating ? dxil_module_get_float_type(t->alu.module,32) : dxil_module_get_int_type(t->alu.module,32),v) : NULL;
  if (!v) t->alu.failed = 1;
  return v;
}
static XeDxilValue field(TextureStage *t, XeDxilValue v, unsigned shift, unsigned mask) { return bin(t,DXIL_BINOP_AND,bin(t,DXIL_BINOP_LSHR,v,ic(t,shift)),ic(t,mask)); }
static XeDxilValue signed_bits(TextureStage *t, XeDxilValue v, unsigned shift, unsigned width) {
  return bin(t,DXIL_BINOP_ASHR,bin(t,DXIL_BINOP_SHL,v,ic(t,32-shift-width)),ic(t,32-width));
}
static XeDxilValue unary(TextureStage *t, unsigned opcode, XeDxilValue v) {
  const struct dxil_func *function = dxil_get_function(t->alu.module,"dx.op.unary",DXIL_F32); XeDxilValue args[2];
  args[0] = ic(t,opcode); args[1] = v;
  v = function && v ? dxil_emit_call(t->alu.module,function,args,2) : NULL; if (!v) t->alu.failed = 1; return v;
}
static XeDxilValue maximum(TextureStage *t, XeDxilValue a, XeDxilValue b) {
  const struct dxil_func *function = dxil_get_function(t->alu.module,"dx.op.binary",DXIL_F32); XeDxilValue args[3], v;
  args[0] = ic(t,35); args[1] = a; args[2] = b;
  v = function && a && b ? dxil_emit_call(t->alu.module,function,args,3) : NULL; if (!v) t->alu.failed = 1; return v;
}
static void begin(TextureStage *t, unsigned block) { t->alu.module->cur_emitting_func->basic_block_ids[block] = (int)t->alu.module->cur_emitting_func->curr_block; t->current_block = block; }
/* All metadata loads dominate guest control flow. Reuse each 16-byte CBV
 * vector and extract a constant lane instead of emitting dynamic lane selects. */
static XeDxilValue cached_word(TextureStage *t, XeDxilValue handle, unsigned word,
    unsigned base_vector, XeDxilValue *vectors, unsigned count) {
  const struct dxil_func *function; XeDxilValue args[3], value; unsigned vector = word/4;
  if (vector < base_vector || vector-base_vector >= count) { t->alu.failed = 1; return NULL; }
  vector -= base_vector;
  if (!vectors[vector]) {
    function = dxil_get_function(t->alu.module,"dx.op.cbufferLoadLegacy",DXIL_I32);
    args[0] = ic(t,59); args[1] = handle; args[2] = ic(t,word/4);
    vectors[vector] = function ? dxil_emit_call(t->alu.module,function,args,3) : NULL;
  }
  value = vectors[vector] ? dxil_emit_extractval(t->alu.module,vectors[vector],word%4) : NULL;
  if (!value) t->alu.failed = 1;
  return value;
}
XeDxilTextureStage *xe_dxil_texture_stage_create(XeDxilShaderModule *shader, XeDxilValue system,
    XeDxilValue fetch, const XeDxilTextureStageOptions *o) {
  TextureStage *t; struct dxil_module *m; unsigned n, j, words, dimension, declared = 0, seen = 0; int resource;
  XeDxilValue fetch_vectors[48], sign_vectors[3], integer_vectors[9];
  if (!shader || !system || !fetch || !o || !layout_valid(o->layout) || !o->layout->texture_count || !o->layout->sampler_count || o->system_constants_bytes > 65536) return NULL;
  m = xe_dxil_shader_module(shader); if (!m || m->minor_version < 6 || !m->cur_emitting_func || m->cur_emitting_func->curr_block ||
      (o->pixel_shader ? m->shader_kind != DXIL_PIXEL_SHADER : (m->shader_kind != DXIL_VERTEX_SHADER && m->shader_kind != DXIL_COMPUTE_SHADER))) return NULL;
  t = (TextureStage *)calloc(1,sizeof(*t)); if (!t) return NULL;
  memset((void *)fetch_vectors,0,sizeof(fetch_vectors)); memset((void *)sign_vectors,0,sizeof(sign_vectors)); memset((void *)integer_vectors,0,sizeof(integer_vectors));
  t->shader = shader; t->options = *o; t->system = system; t->fetch = fetch; xe_dxil_alu_init(&t->alu,m); words = o->system_constants_bytes/4;
  resource = xe_dxil_shader_resource(shader,XE_DXIL_RESOURCE_SRV,DXIL_RESOURCE_KIND_RAW_BUFFER,0,o->pixel_shader ? 3 : 2,1,0);
  if (resource < 0) goto fail;
  t->indices = xe_dxil_resource_handle(&t->alu,XE_DXIL_RESOURCE_SRV,(unsigned)resource,o->pixel_shader ? 3 : 2);
  for (n = 0; n < o->layout->texture_count; ++n) {
    dimension = o->layout->textures[n].dimension;
    if (dimension < 1 || dimension > 3) goto fail;
    if (declared&(1u<<dimension)) continue;
    resource = xe_dxil_shader_resource(shader,XE_DXIL_RESOURCE_SRV,dimension == 3 ? DXIL_RESOURCE_KIND_TEXTURECUBE : dimension == 2 ? DXIL_RESOURCE_KIND_TEXTURE3D : DXIL_RESOURCE_KIND_TEXTURE2D_ARRAY,o->pixel_shader ? 3 : 2,dimension-1,1,0);
    if (resource < 0) goto fail;
    t->texture_resource[dimension] = (unsigned)resource; declared |= 1u<<dimension;
  }
  resource = xe_dxil_shader_resource(shader,XE_DXIL_RESOURCE_SAMPLER,DXIL_RESOURCE_KIND_SAMPLER,o->pixel_shader ? 3 : 2,0,1,0);
  if (resource < 0) goto fail;
  t->sampler_resource = (unsigned)resource;
  for (n = 0; n < o->layout->texture_count; ++n) {
    unsigned f = o->layout->textures[n].fetch_constant;
    if (f > 31 || (o->layout->textures[n].dimension < 1 || o->layout->textures[n].dimension > 3) || o->layout->textures[n].is_signed > 1 || o->signs_word >= words || f/4 >= words-o->signs_word || o->integer_scale_word >= words || f >= words-o->integer_scale_word) goto fail;
    if (seen&(1u<<f)) continue;
    seen |= 1u<<f;
    t->signs[f] = field(t,cached_word(t,system,o->signs_word+f/4,o->signs_word/4,sign_vectors,3),(f%4)*8,255);
    t->integer_bits[f] = cached_word(t,system,o->integer_scale_word+f,o->integer_scale_word/4,integer_vectors,9);
    for (j = 0; j < 4; ++j) t->constants[f][j] = cached_word(t,fetch,f*6+2+j,0,fetch_vectors,48);
  }
  if (!t->indices || t->alu.failed) goto fail;
  return t;
 fail: free(t); return NULL;
}
void xe_dxil_texture_stage_destroy(XeDxilTextureStage *t) { free(t); }
static int sample_if(TextureStage *t, XeDxilValue condition, unsigned binding, unsigned sampler,
    const XeDxilTextureSample *source, XeDxilValue result[4]) {
  struct dxil_module *m = t->alu.module; XeDxilTextureSample sample = *source;
  XeDxilValue index, values[4], incoming[2]; struct dxil_instr *phi; unsigned blocks, before, predecessors[2], j;
  before = t->current_block;
  if (!condition || !dxil_function_reserve_blocks(m,2,&blocks) || !dxil_emit_branch(m,condition,blocks,blocks+1)) return 0;
  begin(t,blocks);
  index = xe_dxil_raw_buffer_load(&t->alu,t->indices,ic(t,binding*8));
  sample.texture = xe_dxil_shader_heap_handle(t->shader,XE_DXIL_RESOURCE_SRV,t->texture_resource[sample.dimension == XE_DXIL_SAMPLE_CUBE ? 3 : sample.dimension == XE_DXIL_SAMPLE_3D ? 2 : 1],index,0);
  index = xe_dxil_raw_buffer_load(&t->alu,t->indices,ic(t,(t->options.layout->texture_count+sampler)*8+4));
  sample.sampler = xe_dxil_shader_heap_handle(t->shader,XE_DXIL_RESOURCE_SAMPLER,t->sampler_resource,index,0);
  if (!xe_dxil_texture_sample(&t->alu,&sample,values) || !dxil_emit_branch(m,NULL,blocks+1,0)) return 0;
  begin(t,blocks+1); predecessors[0] = before; predecessors[1] = blocks;
  for (j = 0; j < 4; ++j) {
    incoming[0] = fc(t,0); incoming[1] = values[j]; phi = dxil_emit_phi(m,dxil_module_get_float_type(m,32));
    if (!phi || !dxil_phi_add_incoming(phi,incoming,predecessors,2)) return 0;
    result[j] = dxil_instr_get_return_value(phi);
  }
  return !t->alu.failed;
}
static XeDxilValue lod_if(TextureStage *t, XeDxilValue condition, unsigned binding,
    unsigned sampler, const XeDxilTextureSample *sample) {
  struct dxil_module *m = t->alu.module; const struct dxil_func *function;
  XeDxilValue index, args[7], value, incoming[2]; struct dxil_instr *phi;
  unsigned blocks, before = t->current_block, predecessors[2];
  if (!condition || !dxil_function_reserve_blocks(m,2,&blocks) || !dxil_emit_branch(m,condition,blocks,blocks+1)) return NULL;
  begin(t,blocks); index = xe_dxil_raw_buffer_load(&t->alu,t->indices,ic(t,binding*8));
  args[0] = ic(t,81);
  args[1] = xe_dxil_shader_heap_handle(t->shader,XE_DXIL_RESOURCE_SRV,t->texture_resource[sample->dimension == XE_DXIL_SAMPLE_CUBE ? 3 : sample->dimension == XE_DXIL_SAMPLE_3D ? 2 : 1],index,0);
  index = xe_dxil_raw_buffer_load(&t->alu,t->indices,ic(t,(t->options.layout->texture_count+sampler)*8+4));
  args[2] = xe_dxil_shader_heap_handle(t->shader,XE_DXIL_RESOURCE_SAMPLER,t->sampler_resource,index,0);
  args[3] = sample->coordinates[0]; args[4] = sample->coordinates[1];
  args[5] = sample->dimension == XE_DXIL_SAMPLE_2D_ARRAY ? dxil_module_get_undef(m,dxil_module_get_float_type(m,32)) : sample->coordinates[2];
  args[6] = dxil_module_get_int1_const(m,0); function = dxil_get_function(m,"dx.op.calculateLOD",DXIL_F32);
  value = function && args[1] && args[2] ? dxil_emit_call(m,function,args,7) : NULL;
  if (!value || !dxil_emit_branch(m,NULL,blocks+1,0)) return NULL;
  begin(t,blocks+1); incoming[0] = fc(t,0); incoming[1] = value; predecessors[0] = before; predecessors[1] = blocks;
  phi = dxil_emit_phi(m,dxil_module_get_float_type(m,32));
  return phi && dxil_phi_add_incoming(phi,incoming,predecessors,2) ? dxil_instr_get_return_value(phi) : NULL;
}
/* The runtime dimensionality is uniform. Unselected views and a zero-weight
 * second array layer never issue a resource sample. */
static int volume_samples(TextureStage *t, const XeDxilTextureInstruction *i,
    const XeDxilTextureSample *source, XeDxilValue is_3d, XeDxilValue depth,
    unsigned sampler, const int bindings[4], XeDxilValue needed[2], XeDxilValue values[2][4]) {
  XeDxilTextureSample sample = *source;
  XeDxilValue volume[4], first[4], second[4], condition, stack, linear, mag, min, layer, fraction, next, gradient, filtered;
  unsigned sign, j;
  stack = cmp(t,DXIL_ICMP_EQ,is_3d,dxil_module_get_int1_const(t->alu.module,0));
  mag = i->volume_mag_filter == 3 ? cmp(t,DXIL_ICMP_NE,field(t,t->constants[i->constant_index][2],0,1),ic(t,0)) : dxil_module_get_int1_const(t->alu.module,i->volume_mag_filter == 1);
  min = i->volume_min_filter == 3 ? cmp(t,DXIL_ICMP_NE,field(t,t->constants[i->constant_index][2],1,1),ic(t,0)) : dxil_module_get_int1_const(t->alu.module,i->volume_min_filter == 1);
  linear = mag;
  if (sample.mode == XE_DXIL_SAMPLE_GRAD) {
    gradient = bin(t,DXIL_BINOP_MUL,maximum(t,unary(t,6,sample.ddx[2]),unary(t,6,sample.ddy[2])),depth);
    linear = pick(t,cmp(t,DXIL_FCMP_OGT,gradient,fc(t,1)),min,mag);
  }
  layer = bin(t,DXIL_BINOP_SUB,source->coordinates[2],pick(t,linear,fc(t,.5f),fc(t,0)));
  next = unary(t,27,layer); fraction = bin(t,DXIL_BINOP_SUB,layer,next);
  condition = bin(t,DXIL_BINOP_AND,linear,cmp(t,DXIL_FCMP_OGT,fraction,fc(t,0)));
  for (sign = 0; sign < 2; ++sign) {
    sample = *source; sample.dimension = XE_DXIL_SAMPLE_3D;
    if (!sample_if(t,bin(t,DXIL_BINOP_AND,needed[sign],is_3d),(unsigned)bindings[2+sign],sampler,&sample,volume)) return 0;
    sample.dimension = XE_DXIL_SAMPLE_2D_ARRAY; sample.coordinates[2] = next;
    if (!sample_if(t,bin(t,DXIL_BINOP_AND,needed[sign],stack),(unsigned)bindings[sign],sampler,&sample,first)) return 0;
    sample.coordinates[2] = bin(t,DXIL_BINOP_ADD,next,fc(t,1));
    if (!sample_if(t,bin(t,DXIL_BINOP_AND,bin(t,DXIL_BINOP_AND,needed[sign],stack),condition),(unsigned)bindings[sign],sampler,&sample,second)) return 0;
    for (j = 0; j < 4; ++j) {
      filtered = bin(t,DXIL_BINOP_ADD,first[j],bin(t,DXIL_BINOP_MUL,bin(t,DXIL_BINOP_SUB,second[j],first[j]),fraction));
      values[sign][j] = pick(t,is_3d,volume[j],pick(t,condition,filtered,first[j]));
    }
  }
  return !t->alu.failed;
}
static unsigned point_allowed(const XeDxilTextureInstruction *i, unsigned pixel) {
  return i->mag_filter != 1 && i->min_filter != 1 && i->mip_filter != 1 &&
    (!computed_lod(i,pixel) || i->aniso_filter == 0 || i->aniso_filter == 7);
}
static void cube_coordinates(TextureStage *t, XeDxilTextureSample *sample, XeDxilValue face, int offset) {
  XeDxilValue s, v, ns, nv, axis, negative, sign, x, y, z;
  s = bin(t,DXIL_BINOP_ADD,bin(t,DXIL_BINOP_MUL,sample->coordinates[0],fc(t,2)),fc(t,-3));
  v = bin(t,DXIL_BINOP_ADD,bin(t,DXIL_BINOP_MUL,sample->coordinates[1],fc(t,2)),fc(t,-3));
  face = maximum(t,bin(t,DXIL_BINOP_ADD,face,fc(t,(float)offset*.5f)),fc(t,0));
  face = pick(t,cmp(t,DXIL_FCMP_OLT,face,fc(t,5)),face,fc(t,5));
  face = cast(t,DXIL_CAST_FPTOUI,0,face);
  axis = bin(t,DXIL_BINOP_LSHR,face,ic(t,1)); negative = cmp(t,DXIL_ICMP_NE,bin(t,DXIL_BINOP_AND,face,ic(t,1)),ic(t,0));
  sign = pick(t,negative,fc(t,-1),fc(t,1)); ns = bin(t,DXIL_BINOP_SUB,fc(t,0),s); nv = bin(t,DXIL_BINOP_SUB,fc(t,0),v);
  x = pick(t,cmp(t,DXIL_ICMP_EQ,axis,ic(t,0)),sign,pick(t,cmp(t,DXIL_ICMP_EQ,axis,ic(t,1)),s,pick(t,negative,ns,s)));
  y = pick(t,cmp(t,DXIL_ICMP_EQ,axis,ic(t,1)),sign,nv);
  z = pick(t,cmp(t,DXIL_ICMP_EQ,axis,ic(t,0)),pick(t,negative,s,ns),pick(t,cmp(t,DXIL_ICMP_EQ,axis,ic(t,1)),pick(t,negative,nv,v),sign));
  sample->coordinates[0] = x; sample->coordinates[1] = y; sample->coordinates[2] = z;
}
static XeDxilValue sign_transform(TextureStage *t, XeDxilValue sign, XeDxilValue integer_bits,
    unsigned component, XeDxilValue value) {
  struct dxil_module *m = t->alu.module; XeDxilValue incoming[3], half, exact, biased;
  unsigned blocks, predecessors[3]; struct dxil_instr *phi;
  if (!dxil_function_reserve_blocks(m,4,&blocks) || !dxil_emit_branch(m,cmp(t,DXIL_ICMP_EQ,sign,ic(t,2)),blocks,blocks+1)) return NULL;
  begin(t,blocks);
  half = cast(t,DXIL_CAST_UITOFP,1,bin(t,DXIL_BINOP_SHL,ic(t,1),field(t,integer_bits,component*6,15)));
  exact = bin(t,DXIL_BINOP_SDIV,bin(t,DXIL_BINOP_SUB,
    bin(t,DXIL_BINOP_MUL,value,bin(t,DXIL_BINOP_SUB,bin(t,DXIL_BINOP_MUL,half,fc(t,2)),fc(t,1))),half),bin(t,DXIL_BINOP_SUB,half,fc(t,1)));
  biased = bin(t,DXIL_BINOP_SUB,bin(t,DXIL_BINOP_MUL,value,fc(t,2)),fc(t,1));
  incoming[0] = pick(t,cmp(t,DXIL_ICMP_NE,field(t,integer_bits,component*6,15),ic(t,0)),exact,biased); predecessors[0] = blocks;
  if (!incoming[0] || !dxil_emit_branch(m,NULL,blocks+3,0)) return NULL;
  begin(t,blocks+1);
  if (!dxil_emit_branch(m,cmp(t,DXIL_ICMP_EQ,sign,ic(t,3)),blocks+2,blocks+3)) return NULL;
  incoming[1] = value; predecessors[1] = blocks+1;
  begin(t,blocks+2); incoming[2] = xe_dxil_gamma_decode(&t->alu,xe_dxil_alu_saturate(&t->alu,value)); predecessors[2] = blocks+2;
  if (!incoming[2] || !dxil_emit_branch(m,NULL,blocks+3,0)) return NULL;
  begin(t,blocks+3); phi = dxil_emit_phi(m,dxil_module_get_float_type(m,32));
  return phi && dxil_phi_add_incoming(phi,incoming,predecessors,3) ? dxil_instr_get_return_value(phi) : NULL;
}
static XeDxilValue fixed_grid(TextureStage *t, XeDxilValue bits, unsigned component, XeDxilValue value) {
  struct dxil_module *m = t->alu.module; XeDxilValue lane, condition, power, incoming[2]; unsigned blocks, predecessors[2]; struct dxil_instr *phi;
  lane = field(t,bits,component*6,63); condition = cmp(t,DXIL_ICMP_ULT,bin(t,DXIL_BINOP_SUB,lane,ic(t,1)),ic(t,15));
  condition = bin(t,DXIL_BINOP_AND,condition,cmp(t,DXIL_ICMP_NE,field(t,bits,26,1),ic(t,0)));
  predecessors[0] = t->current_block; incoming[0] = value;
  if (!dxil_function_reserve_blocks(m,2,&blocks) || !dxil_emit_branch(m,condition,blocks,blocks+1)) return NULL;
  begin(t,blocks);
  power = cast(t,DXIL_CAST_UITOFP,1,bin(t,DXIL_BINOP_SHL,ic(t,2),field(t,lane,0,15)));
  incoming[1] = bin(t,DXIL_BINOP_SDIV,bin(t,DXIL_BINOP_MUL,
    unary(t,26,bin(t,DXIL_BINOP_MUL,value,bin(t,DXIL_BINOP_SUB,power,fc(t,1)))),
    bin(t,DXIL_BINOP_ADD,power,fc(t,1))),bin(t,DXIL_BINOP_MUL,power,power));
  predecessors[1] = blocks;
  if (!incoming[1] || !dxil_emit_branch(m,NULL,blocks+1,0)) return NULL;
  begin(t,blocks+1); phi = dxil_emit_phi(m,dxil_module_get_float_type(m,32));
  return phi && dxil_phi_add_incoming(phi,incoming,predecessors,2) ? dxil_instr_get_return_value(phi) : NULL;
}
static int numeric_format(TextureStage *t, XeDxilValue bits, unsigned used, unsigned point,
    XeDxilValue values[4]) {
  struct dxil_module *m = t->alu.module; XeDxilValue normalized[4], integer[4], converted[4], incoming[2], lane, sign, value, shift, scale, minimum;
  struct dxil_instr *phi; unsigned blocks, before, predecessors[2], norm_predecessor, i;
  before = t->current_block;
  if (!dxil_function_reserve_blocks(m,4,&blocks) || !dxil_emit_branch(m,
      cmp(t,DXIL_ICMP_NE,bin(t,DXIL_BINOP_AND,bits,ic(t,0x3ffffff)),ic(t,0)),blocks,blocks+3)) return 0;
  begin(t,blocks);
  if (!dxil_emit_branch(m,cmp(t,DXIL_ICMP_NE,field(t,bits,24,1),ic(t,0)),blocks+1,blocks+2)) return 0;
  begin(t,blocks+1);
  for (i = 0; i < 4; ++i) {
    normalized[i] = values[i]; if (!(used&(1u<<i))) continue;
    value = point ? fixed_grid(t,bits,i,values[i]) : values[i]; if (!value) return 0;
    sign = field(t,bits,i*6+4,3);
    normalized[i] = pick(t,cmp(t,DXIL_ICMP_EQ,sign,ic(t,0)),
      bin(t,DXIL_BINOP_MUL,unary(t,26,bin(t,DXIL_BINOP_MUL,value,fc(t,65536))),fc(t,1.0f/65536)),value);
    normalized[i] = pick(t,cmp(t,DXIL_ICMP_EQ,sign,ic(t,2)),maximum(t,normalized[i],fc(t,-1)),normalized[i]);
  }
  norm_predecessor = t->current_block;
  /* A fifth block joins normalized/integer conversion before the outer merge. */
  {
    unsigned merge;
    if (!dxil_function_reserve_blocks(m,1,&merge) || !dxil_emit_branch(m,NULL,merge,0)) return 0;
    begin(t,blocks+2);
    for (i = 0; i < 4; ++i) {
      integer[i] = values[i]; if (!(used&(1u<<i))) continue;
      lane = field(t,bits,i*6,63); sign = field(t,lane,4,3);
      shift = bin(t,DXIL_BINOP_ADD,field(t,lane,0,15),ic(t,1));
      shift = bin(t,DXIL_BINOP_SUB,shift,pick(t,cmp(t,DXIL_ICMP_ULT,bin(t,DXIL_BINOP_SUB,sign,ic(t,1)),ic(t,2)),ic(t,1),ic(t,0)));
      scale = cast(t,DXIL_CAST_UITOFP,1,bin(t,DXIL_BINOP_SUB,bin(t,DXIL_BINOP_SHL,ic(t,1),shift),ic(t,1)));
      minimum = pick(t,cmp(t,DXIL_FCMP_OLT,scale,fc(t,.5f)),scale,fc(t,.5f));
      value = bin(t,DXIL_BINOP_ADD,bin(t,DXIL_BINOP_MUL,values[i],maximum(t,scale,fc(t,.5f))),bin(t,DXIL_BINOP_SUB,minimum,fc(t,.5f)));
      integer[i] = point ? pick(t,cmp(t,DXIL_ICMP_NE,field(t,bits,26,1),ic(t,0)),unary(t,26,value),value) : value;
    }
    predecessors[0] = norm_predecessor; predecessors[1] = t->current_block;
    if (!dxil_emit_branch(m,NULL,merge,0)) return 0;
    begin(t,merge);
    for (i = 0; i < 4; ++i) {
      incoming[0] = normalized[i]; incoming[1] = integer[i]; phi = dxil_emit_phi(m,dxil_module_get_float_type(m,32));
      if (!phi || !dxil_phi_add_incoming(phi,incoming,predecessors,2)) return 0;
      converted[i] = dxil_instr_get_return_value(phi);
    }
    predecessors[0] = before; predecessors[1] = merge;
    if (!dxil_emit_branch(m,NULL,blocks+3,0)) return 0;
  }
  begin(t,blocks+3);
  for (i = 0; i < 4; ++i) {
    incoming[0] = values[i]; incoming[1] = converted[i]; phi = dxil_emit_phi(m,dxil_module_get_float_type(m,32));
    if (!phi || !dxil_phi_add_incoming(phi,incoming,predecessors,2)) return 0;
    values[i] = dxil_instr_get_return_value(phi);
  }
  return !t->alu.failed;
}
int xe_dxil_texture_stage_fetch(XeDxilTextureStage *stage, XeDxilAlu *alu, XeDxilAluIo *io,
    const XeDxilTextureInstruction *i, const XeDxilTextureOperands *op, XeDxilValue result[4]) {
  TextureStage *t = stage; XeDxilTextureSample sample; XeDxilTextureInstruction query; XeDxilValue sizes[3], coordinates[2], grad_coordinates[3], is_3d, needed[2], volumes[2][4], original_width, actual_1d, wide, rows, linear_x, row, lod, scale, shift, value, snap, signs[4], any_signed, any_unsigned, unsigned_values[4], signed_values[4], exponent;
  float epsilon;
  unsigned f, j, point, computed, components; int texture[4], sampler;
  if (!t || !alu || alu->module != t->alu.module || !io || !io->current_block || !i || !op || !result) return 0;
  if (i->opcode == 16 || (i->opcode == 17 && (!t->options.pixel_shader || !i->computed_lod || i->register_gradients))) { for (j = 0; j < 4; ++j) result[j] = fc(t,0); return !t->alu.failed; }
  if (i->opcode == 19) return xe_dxil_texture_weights(alu,i,op,t->fetch,result);
  if ((i->opcode != 1 && i->opcode != 17) || i->dimension > 3 || i->constant_index > 31 || !op->coordinate_count || (i->dimension && op->coordinate_count < (i->dimension >= 2 ? 3u : 2u))) return 0;
  if (i->opcode == 17) { query = *i; memset(query.offset_halves,0,sizeof(query.offset_halves)); i = &query; }
  f = i->constant_index; if (!t->signs[f] || !t->integer_bits[f]) return 0;
  for (j = 0; j < 2; ++j) { texture[j] = xe_dxil_texture_layout_texture(t->options.layout,f,i->dimension == 3 ? 3 : 1,j); if (texture[j] < 0) return 0; }
  if (i->dimension == 2) for (j = 0; j < 2; ++j) { texture[2+j] = xe_dxil_texture_layout_texture(t->options.layout,f,2,j); if (texture[2+j] < 0) return 0; }
  sampler = xe_dxil_texture_layout_sampler(t->options.layout,i,t->options.pixel_shader); if (sampler < 0) return 0;
  t->current_block = *io->current_block; point = point_allowed(i,t->options.pixel_shader); epsilon = i->opcode == 17 ? 0.0f : 1.5f/1024; computed = computed_lod(i,t->options.pixel_shader);
  memset(&sample,0,sizeof(sample)); sample.dimension = XE_DXIL_SAMPLE_2D_ARRAY; sample.mode = computed ? XE_DXIL_SAMPLE_GRAD : XE_DXIL_SAMPLE_LEVEL;
  components = i->dimension >= 2 ? 3 : !i->dimension && op->coordinate_count == 1 ? 1 : 2;
  actual_1d = original_width = NULL; wide = dxil_module_get_int1_const(t->alu.module,0);
  if (!i->dimension) {
    actual_1d = cmp(t,DXIL_ICMP_EQ,field(t,t->constants[f][3],9,3),ic(t,0));
    original_width = bin(t,DXIL_BINOP_ADD,field(t,t->constants[f][0],0,16777215),ic(t,1));
  }
  is_3d = i->dimension == 2 ? cmp(t,DXIL_ICMP_EQ,field(t,t->constants[f][3],9,3),ic(t,2)) : NULL;
  snap = i->opcode == 1 && i->dimension == 1 && !i->unnormalized && point ? cmp(t,DXIL_ICMP_NE,field(t,t->integer_bits[f],26,1),ic(t,0)) : dxil_module_get_int1_const(t->alu.module,0);
  for (j = 0; j < 2; ++j) {
    sizes[j] = bin(t,DXIL_BINOP_ADD,field(t,t->constants[f][0],j*13,8191),ic(t,1));
    if (i->dimension == 2) sizes[j] = pick(t,is_3d,bin(t,DXIL_BINOP_ADD,field(t,t->constants[f][0],j*11,2047),ic(t,1)),sizes[j]);
    if (!i->dimension) sizes[j] = components == 1 ? (j ? ic(t,1) : original_width) : pick(t,actual_1d,j ? ic(t,1) : original_width,sizes[j]);
    if (i->opcode == 1 && i->unnormalized && i->dimension == 1) {
      shift = field(t,t->constants[f][2],2,15); value = bin(t,DXIL_BINOP_LSHR,sizes[j],shift);
      value = pick(t,cmp(t,DXIL_ICMP_NE,value,ic(t,0)),value,ic(t,1));
      sizes[j] = pick(t,cmp(t,DXIL_ICMP_EQ,shift,field(t,t->constants[f][2],6,15)),value,sizes[j]);
    }
    sizes[j] = cast(t,DXIL_CAST_UITOFP,1,sizes[j]); value = j < components ? op->coordinates[j] : fc(t,0);
    if (i->unnormalized) coordinates[j] = bin(t,DXIL_BINOP_SDIV,bin(t,DXIL_BINOP_ADD,value,fc(t,(float)i->offset_halves[j]*.5f)),sizes[j]);
    else coordinates[j] = bin(t,DXIL_BINOP_ADD,value,bin(t,DXIL_BINOP_SDIV,fc(t,(float)i->offset_halves[j]*.5f),sizes[j]));
    grad_coordinates[j] = !i->unnormalized && point ? coordinates[j] : bin(t,DXIL_BINOP_ADD,coordinates[j],bin(t,DXIL_BINOP_SDIV,fc(t,epsilon),sizes[j]));
    sample.coordinates[j] = pick(t,snap,
      bin(t,DXIL_BINOP_SDIV,bin(t,DXIL_BINOP_ADD,unary(t,27,bin(t,DXIL_BINOP_MUL,coordinates[j],sizes[j])),fc(t,.5f)),sizes[j]),
      bin(t,DXIL_BINOP_ADD,coordinates[j],bin(t,DXIL_BINOP_SDIV,fc(t,epsilon),sizes[j])));
  }
  if (components == 1) { sample.coordinates[1] = coordinates[1] = grad_coordinates[1] = fc(t,0); }
  if (!i->dimension) {
    wide = bin(t,DXIL_BINOP_AND,actual_1d,cmp(t,DXIL_ICMP_UGT,original_width,ic(t,8192)));
    original_width = cast(t,DXIL_CAST_UITOFP,1,original_width);
    rows = unary(t,28,bin(t,DXIL_BINOP_SDIV,original_width,fc(t,8192)));
    rows = pick(t,cmp(t,DXIL_FCMP_OLT,rows,fc(t,64)),rows,fc(t,64));
    linear_x = bin(t,DXIL_BINOP_MUL,sample.coordinates[0],original_width);
    row = unary(t,27,bin(t,DXIL_BINOP_SDIV,linear_x,fc(t,8192)));
    value = bin(t,DXIL_BINOP_SDIV,bin(t,DXIL_BINOP_SUB,linear_x,bin(t,DXIL_BINOP_MUL,row,fc(t,8192))),fc(t,8192));
    sample.coordinates[0] = pick(t,wide,value,sample.coordinates[0]);
    value = bin(t,DXIL_BINOP_SDIV,bin(t,DXIL_BINOP_ADD,row,fc(t,.5f)),rows);
    sample.coordinates[1] = pick(t,wide,value,sample.coordinates[1]);
    if (components == 1 || (computed && !i->register_gradients)) { grad_coordinates[0] = sample.coordinates[0]; grad_coordinates[1] = sample.coordinates[1]; }
  }
  sample.coordinates[2] = fc(t,0);
  if (i->dimension == 2) {
    sizes[2] = cast(t,DXIL_CAST_UITOFP,1,bin(t,DXIL_BINOP_ADD,pick(t,is_3d,field(t,t->constants[f][0],22,1023),field(t,t->constants[f][0],26,63)),ic(t,1)));
    value = fc(t,(float)i->offset_halves[2]*.5f+epsilon);
    if (i->unnormalized) {
      shift = bin(t,DXIL_BINOP_ADD,op->coordinates[2],value);
      sample.coordinates[2] = pick(t,is_3d,bin(t,DXIL_BINOP_SDIV,shift,sizes[2]),shift);
    } else {
      shift = bin(t,DXIL_BINOP_ADD,bin(t,DXIL_BINOP_MUL,op->coordinates[2],sizes[2]),value);
      sample.coordinates[2] = pick(t,is_3d,bin(t,DXIL_BINOP_ADD,op->coordinates[2],bin(t,DXIL_BINOP_SDIV,value,sizes[2])),shift);
    }
    value = maximum(t,sample.coordinates[2],fc(t,0));
    value = pick(t,cmp(t,DXIL_FCMP_OLT,value,sizes[2]),value,sizes[2]);
    sample.coordinates[2] = pick(t,is_3d,sample.coordinates[2],value);
    grad_coordinates[2] = sample.coordinates[2];
  }
  if (i->dimension == 3) { sample.dimension = XE_DXIL_SAMPLE_CUBE; cube_coordinates(t,&sample,op->coordinates[2],i->offset_halves[2]); }
  if (i->opcode == 17) {
    XeDxilValue signed_query, unsigned_query, signed_condition, unsigned_condition, a, b;
    signed_condition = cmp(t,DXIL_ICMP_EQ,t->signs[f],ic(t,0x55));
    unsigned_condition = cmp(t,DXIL_ICMP_EQ,signed_condition,dxil_module_get_int1_const(t->alu.module,0));
    if (i->dimension == 2) {
      sample.dimension = XE_DXIL_SAMPLE_3D;
      a = lod_if(t,bin(t,DXIL_BINOP_AND,signed_condition,is_3d),(unsigned)texture[3],(unsigned)sampler,&sample);
      b = lod_if(t,bin(t,DXIL_BINOP_AND,unsigned_condition,is_3d),(unsigned)texture[2],(unsigned)sampler,&sample);
      value = cmp(t,DXIL_ICMP_EQ,is_3d,dxil_module_get_int1_const(t->alu.module,0)); sample.dimension = XE_DXIL_SAMPLE_2D_ARRAY;
      signed_query = lod_if(t,bin(t,DXIL_BINOP_AND,signed_condition,value),(unsigned)texture[1],(unsigned)sampler,&sample);
      unsigned_query = lod_if(t,bin(t,DXIL_BINOP_AND,unsigned_condition,value),(unsigned)texture[0],(unsigned)sampler,&sample);
      signed_query = pick(t,is_3d,a,signed_query); unsigned_query = pick(t,is_3d,b,unsigned_query);
    } else {
      signed_query = lod_if(t,signed_condition,(unsigned)texture[1],(unsigned)sampler,&sample);
      unsigned_query = lod_if(t,unsigned_condition,(unsigned)texture[0],(unsigned)sampler,&sample);
    }
    result[0] = pick(t,signed_condition,signed_query,unsigned_query);
    for (j = 1; j < 4; ++j) result[j] = fc(t,0);
    *io->current_block = t->current_block; return result[0] && !t->alu.failed;
  }
  lod = bin(t,DXIL_BINOP_MUL,cast(t,DXIL_CAST_SITOFP,1,signed_bits(t,t->constants[f][2],12,10)),fc(t,1.0f/32));
  if (i->register_lod) lod = bin(t,DXIL_BINOP_ADD,op->lod,lod);
  if (i->lod_bias_sixteenths) lod = bin(t,DXIL_BINOP_ADD,lod,fc(t,(float)i->lod_bias_sixteenths/16));
  if (i->dimension >= 2 && computed && !i->register_gradients) {
    value = signed_bits(t,t->constants[f][2],22,5); shift = signed_bits(t,t->constants[f][2],27,5);
    value = pick(t,cmp(t,DXIL_ICMP_SGT,value,shift),value,shift);
    lod = bin(t,DXIL_BINOP_ADD,lod,cast(t,DXIL_CAST_SITOFP,1,value)); sample.mode = XE_DXIL_SAMPLE_BIAS;
  }
  sample.lod_or_bias = lod;
  if (computed && sample.mode == XE_DXIL_SAMPLE_GRAD) for (j = 0; j < (i->dimension >= 2 ? 3u : 2u); ++j) {
    shift = signed_bits(t,t->constants[f][2],22,5);
    scale = unary(t,21,bin(t,DXIL_BINOP_ADD,lod,cast(t,DXIL_CAST_SITOFP,1,shift)));
    sample.ddx[j] = bin(t,DXIL_BINOP_MUL,i->register_gradients && (components > 1 || !t->options.pixel_shader) ?
      (i->dimension == 3 ? op->ddx[j] : j >= components ? fc(t,0) : i->unnormalized ? bin(t,DXIL_BINOP_SDIV,op->ddx[j],sizes[j]) : op->ddx[j]) : unary(t,83,grad_coordinates[j]),scale);
    shift = signed_bits(t,t->constants[f][2],27,5);
    scale = unary(t,21,bin(t,DXIL_BINOP_ADD,lod,cast(t,DXIL_CAST_SITOFP,1,shift)));
    sample.ddy[j] = bin(t,DXIL_BINOP_MUL,i->register_gradients && (components > 1 || !t->options.pixel_shader) ?
      (i->dimension == 3 ? op->ddy[j] : j >= components ? fc(t,0) : i->unnormalized ? bin(t,DXIL_BINOP_SDIV,op->ddy[j],sizes[j]) : op->ddy[j]) : unary(t,84,grad_coordinates[j]),scale);
  }
  if (!i->dimension && computed && i->register_gradients && (components > 1 || !t->options.pixel_shader)) {
    scale = pick(t,wide,bin(t,DXIL_BINOP_SDIV,original_width,fc(t,8192)),fc(t,1));
    sample.ddx[0] = bin(t,DXIL_BINOP_MUL,sample.ddx[0],scale);
    sample.ddy[0] = bin(t,DXIL_BINOP_MUL,sample.ddy[0],scale);
    sample.ddx[1] = pick(t,wide,fc(t,0),sample.ddx[1]); sample.ddy[1] = pick(t,wide,fc(t,0),sample.ddy[1]);
  }
  any_signed = any_unsigned = dxil_module_get_int1_const(t->alu.module,0);
  for (j = 0; j < 4; ++j) {
    signs[j] = field(t,t->signs[f],j*2,3);
    if (!(op->result_components&(1u<<j))) continue;
    value = cmp(t,DXIL_ICMP_EQ,signs[j],ic(t,1)); any_signed = bin(t,DXIL_BINOP_OR,any_signed,value);
    value = cmp(t,DXIL_ICMP_NE,signs[j],ic(t,1)); any_unsigned = bin(t,DXIL_BINOP_OR,any_unsigned,value);
  }
  if (i->dimension == 2) {
    needed[0] = any_unsigned; needed[1] = any_signed;
    if (!volume_samples(t,i,&sample,is_3d,sizes[2],(unsigned)sampler,texture,needed,volumes)) return 0;
    for (j = 0; j < 4; ++j) { unsigned_values[j] = volumes[0][j]; signed_values[j] = volumes[1][j]; }
  } else {
  if (!sample_if(t,any_unsigned,(unsigned)texture[0],(unsigned)sampler,&sample,unsigned_values) ||
      !sample_if(t,any_signed,(unsigned)texture[1],(unsigned)sampler,&sample,signed_values)) return 0;
  }
  for (j = 0; j < 4; ++j) {
    result[j] = fc(t,0); if (!(op->result_components&(1u<<j))) continue;
    value = pick(t,cmp(t,DXIL_ICMP_EQ,signs[j],ic(t,1)),signed_values[j],unsigned_values[j]);
    result[j] = sign_transform(t,signs[j],t->integer_bits[f],j,value); if (!result[j]) return 0;
  }
  if (!numeric_format(t,t->integer_bits[f],op->result_components,point,result)) return 0;
  exponent = bin(t,DXIL_BINOP_SHL,bin(t,DXIL_BINOP_ADD,signed_bits(t,t->constants[f][1],13,6),ic(t,127)),ic(t,23));
  exponent = cast(t,DXIL_CAST_BITCAST,1,exponent);
  for (j = 0; j < 4; ++j) if (op->result_components&(1u<<j)) result[j] = bin(t,DXIL_BINOP_MUL,result[j],exponent);
  *io->current_block = t->current_block;
  return !t->alu.failed;
}
