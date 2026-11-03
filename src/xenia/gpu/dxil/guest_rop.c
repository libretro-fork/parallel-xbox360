/* Native ordered guest render backend. MSVC C89; BSD license.
 * Xenia's canonical EDRAM layout and fixed-function render-backend rules. */
#include "guest_rop.h"
#include "guest_blend.h"
#include "edram_format.h"
#include "edram_depth_stencil.h"
#include "guest_resources.h"
#include <dxil_module.h>
#include <dxil_function.h>
#include <stdlib.h>
struct XeDxilGuestRop {
  XeDxilAlu alu;
  XeDxilGuestRopOptions options;
  XeDxilValue constants, edram, counters, surviving, failed[2];
  unsigned current_block;
};
typedef struct XeDxilGuestRop Rop;
static XeDxilValue ic(Rop *r, uint32_t v) { return dxil_module_get_int32_const(r->alu.module, (int32_t)v); }
static XeDxilValue fc(Rop *r, float v) { return dxil_module_get_float_const(r->alu.module, v); }
static XeDxilValue bin(Rop *r, enum dxil_bin_opcode op, XeDxilValue a, XeDxilValue b) {
  XeDxilValue v = a && b ? dxil_emit_binop(r->alu.module, op, a, b, 0) : NULL;
  if (!v) r->alu.failed = 1;
  return v;
}
static XeDxilValue cmp(Rop *r, enum dxil_cmp_pred op, XeDxilValue a, XeDxilValue b) {
  XeDxilValue v = a && b ? dxil_emit_cmp(r->alu.module, op, a, b) : NULL;
  if (!v) r->alu.failed = 1;
  return v;
}
static XeDxilValue pick(Rop *r, XeDxilValue c, XeDxilValue a, XeDxilValue b) {
  XeDxilValue v = c && a && b ? dxil_emit_select(r->alu.module, c, a, b) : NULL;
  if (!v) r->alu.failed = 1;
  return v;
}
static XeDxilValue cast(Rop *r, enum dxil_cast_opcode op, int floating, XeDxilValue v) {
  v = v ? dxil_emit_cast(r->alu.module, op, floating ? dxil_module_get_float_type(r->alu.module, 32) :
    dxil_module_get_int_type(r->alu.module, 32), v) : NULL;
  if (!v) r->alu.failed = 1;
  return v;
}
static XeDxilValue field(Rop *r, XeDxilValue v, unsigned shift, unsigned mask) {
  return bin(r, DXIL_BINOP_AND, bin(r, DXIL_BINOP_LSHR, v, ic(r, shift)), ic(r, mask));
}
static XeDxilValue flag(Rop *r, XeDxilValue v, unsigned mask) {
  return cmp(r, DXIL_ICMP_NE, bin(r, DXIL_BINOP_AND, v, ic(r, mask)), ic(r, 0));
}
static XeDxilValue word(Rop *r, unsigned index) { return xe_dxil_cbuffer_word(&r->alu, r->constants, ic(r, index)); }
static XeDxilValue call(Rop *r, const char *name, enum overload_type type, XeDxilValue *args, unsigned count) {
  const struct dxil_func *function = dxil_get_function(r->alu.module, name, type);
  XeDxilValue value;
  unsigned i;
  for (i = 0; i < count; ++i) if (!args[i]) { r->alu.failed = 1; return NULL; }
  value = function ? dxil_emit_call(r->alu.module, function, args, count) : NULL;
  if (!value) r->alu.failed = 1;
  return value;
}
static XeDxilValue unary(Rop *r, unsigned opcode, XeDxilValue value) {
  XeDxilValue args[2]; args[0] = ic(r, opcode); args[1] = value;
  return call(r, "dx.op.unary", DXIL_F32, args, 2);
}
static void begin(Rop *r, unsigned block) {
  r->alu.module->cur_emitting_func->basic_block_ids[block] = (int)r->alu.module->cur_emitting_func->curr_block;
  r->current_block = block;
}
static int conditional(Rop *r, XeDxilValue condition, unsigned *done) {
  unsigned blocks;
  if (!condition || !dxil_function_reserve_blocks(r->alu.module, 2, &blocks) ||
      !dxil_emit_branch(r->alu.module, condition, blocks, blocks+1)) return 0;
  begin(r, blocks); *done = blocks+1; return 1;
}
static int end(Rop *r, unsigned done) {
  if (!dxil_emit_branch(r->alu.module, NULL, done, 0)) return 0;
  begin(r, done); return 1;
}
static XeDxilValue quantize(Rop *r, XeDxilValue float24, XeDxilValue depth) {
  XeDxilValue incoming[2];
  struct dxil_instr *phi;
  unsigned blocks, predecessors[2];
  if (!float24 || !depth || !dxil_function_reserve_blocks(r->alu.module, 3, &blocks) ||
      !dxil_emit_branch(r->alu.module, float24, blocks, blocks+1)) return NULL;
  begin(r, blocks);
  incoming[0] = xe_dxil_pack_depth20e4(&r->alu, depth, 1, 0); predecessors[0] = blocks;
  if (!incoming[0] || !dxil_emit_branch(r->alu.module, NULL, blocks+2, 0)) return NULL;
  begin(r, blocks+1);
  incoming[1] = cast(r, DXIL_CAST_FPTOUI, 0,
    unary(r, 26, bin(r, DXIL_BINOP_MUL, depth, fc(r, 16777215.0f))));
  predecessors[1] = blocks+1;
  if (!incoming[1] || !dxil_emit_branch(r->alu.module, NULL, blocks+2, 0)) return NULL;
  begin(r, blocks+2);
  phi = dxil_emit_phi(r->alu.module, dxil_module_get_int_type(r->alu.module, 32));
  if (!phi || !dxil_phi_add_incoming(phi, incoming, predecessors, 2)) return NULL;
  return dxil_instr_get_return_value(phi);
}
static XeDxilValue fw(Rop *r, unsigned index) { return cast(r,DXIL_CAST_BITCAST,1,word(r,index)); }
static XeDxilValue binary_float(Rop *r, unsigned opcode, XeDxilValue a, XeDxilValue b) {
  XeDxilValue args[3]; args[0] = ic(r,opcode); args[1] = a; args[2] = b;
  return call(r,"dx.op.binary",DXIL_F32,args,3);
}
static unsigned format_words(unsigned format) { return format == 5 || format == 7 || format == 15 ? 2 : 1; }
static unsigned format_components(unsigned format) { return format == 14 ? 1 : format == 4 || format == 6 || format == 15 ? 2 : 4; }
static unsigned normalize_format(unsigned format) {
  return format < 8 || format == 10 || format == 12 || format == 14 || format == 15 ? format : 14;
}
static unsigned fixed_component(unsigned format, unsigned component) {
  return format < 3 || format == 4 || format == 5 || format == 10 ||
    ((format == 3 || format == 12) && component == 3);
}
static XeDxilValue clamp_blend(Rop *r, XeDxilValue value, unsigned fixed, XeDxilValue low, XeDxilValue high) {
  if (!fixed) return value;
  value = pick(r,cmp(r,DXIL_FCMP_UNO,value,value),fc(r,0),value);
  value = pick(r,cmp(r,DXIL_FCMP_OLT,value,low),low,value);
  return pick(r,cmp(r,DXIL_FCMP_OGT,value,high),high,value);
}
static XeDxilValue factor_term(Rop *r, XeDxilValue value, XeDxilValue factor,
    XeDxilValue source[4], XeDxilValue destination[4], XeDxilValue constant[4], unsigned component,
    unsigned fixed, XeDxilValue low, XeDxilValue high) {
  XeDxilValue choices[13], selected = fc(r,1), known = dxil_module_get_int1_const(r->alu.module,0), condition;
  unsigned i;
  choices[0] = source[component]; choices[1] = bin(r,DXIL_BINOP_SUB,fc(r,1),choices[0]);
  choices[2] = source[3]; choices[3] = bin(r,DXIL_BINOP_SUB,fc(r,1),choices[2]);
  choices[4] = destination[component]; choices[5] = bin(r,DXIL_BINOP_SUB,fc(r,1),choices[4]);
  choices[6] = destination[3]; choices[7] = bin(r,DXIL_BINOP_SUB,fc(r,1),choices[6]);
  choices[8] = constant[component]; choices[9] = bin(r,DXIL_BINOP_SUB,fc(r,1),choices[8]);
  choices[10] = constant[3]; choices[11] = bin(r,DXIL_BINOP_SUB,fc(r,1),choices[10]);
  choices[12] = binary_float(r,36,source[3],bin(r,DXIL_BINOP_SUB,fc(r,1),destination[3]));
  for (i = 0; i < 13; ++i) {
    condition = cmp(r,DXIL_ICMP_EQ,factor,ic(r,XE_DXIL_BLEND_SOURCE_COLOR+i));
    selected = pick(r,condition,choices[i],selected); known = bin(r,DXIL_BINOP_OR,known,condition);
  }
  value = pick(r,known,bin(r,DXIL_BINOP_MUL,value,selected),value);
  /* The zero enum ignores NaN and infinity in the discarded term. */
  value = pick(r,cmp(r,DXIL_ICMP_EQ,factor,ic(r,XE_DXIL_BLEND_ZERO)),fc(r,0),value);
  return clamp_blend(r,value,fixed,low,high);
}
static XeDxilValue blend_component(Rop *r, XeDxilValue source[4], XeDxilValue destination[4], XeDxilValue constant[4],
    unsigned component, unsigned fixed, XeDxilValue low, XeDxilValue high, XeDxilValue controls) {
  unsigned shift = component == 3 ? 16 : 0;
  XeDxilValue src_factor = field(r,controls,shift,31), dst_factor = field(r,controls,shift+8,31);
  XeDxilValue equation = field(r,controls,shift+5,7), a, b, result;
  a = factor_term(r,source[component],src_factor,source,destination,constant,component,fixed,low,high);
  b = factor_term(r,destination[component],dst_factor,source,destination,constant,component,fixed,low,high);
  result = bin(r,DXIL_BINOP_ADD,a,b);
  result = pick(r,cmp(r,DXIL_ICMP_EQ,equation,ic(r,1)),bin(r,DXIL_BINOP_SUB,a,b),result);
  result = pick(r,cmp(r,DXIL_ICMP_EQ,equation,ic(r,4)),bin(r,DXIL_BINOP_SUB,b,a),result);
  result = pick(r,cmp(r,DXIL_ICMP_EQ,equation,ic(r,2)),binary_float(r,36,a,b),result);
  result = pick(r,cmp(r,DXIL_ICMP_EQ,equation,ic(r,3)),binary_float(r,35,a,b),result);
  return clamp_blend(r,result,fixed,low,high);
}
XeDxilGuestRop *xe_dxil_guest_rop_create(XeDxilShaderModule *shader, XeDxilValue system, const XeDxilGuestRopOptions *options) {
  Rop *r;
  struct dxil_module *m;
  unsigned n, i;
  const XeDxilRovDepthOptions *o;
  if (!shader || !system || !options || options->color_mask > 15) return NULL;
  o = &options->depth;
  if (o->survey || o->msaa > 2 || !o->scale_x || !o->scale_y || o->scale_x > 16 || o->scale_y > 16 ||
      o->system_constants_bytes < 64 || o->system_constants_bytes > 65536 || o->depth_function_shift > 29) return NULL;
  n = o->system_constants_bytes/4;
  if (o->flags_word >= n || o->pitch_word >= n || o->depth_base_word >= n || o->counter_word >= n ||
      o->front_scale_word >= n || o->back_scale_word >= n || o->front_offset_word >= n || o->back_offset_word >= n ||
      o->front_masks_word >= n || o->back_masks_word >= n || o->front_ops_word >= n || o->back_ops_word >= n ||
      options->base_word > n-4 || options->blend_word > n-4 || options->keep_word > n-8 ||
      options->clamp_word > n-16 || options->blend_constant_word > n-4) return NULL;
  for (i = 0; i < 4; ++i) if (options->formats[i] > 15) return NULL;
  m = xe_dxil_shader_module(shader);
  if (!m || m->shader_kind != DXIL_PIXEL_SHADER || !m->cur_emitting_func || m->cur_emitting_func->curr_block) return NULL;
  r = (Rop *)calloc(1,sizeof(*r)); if (!r) return NULL;
  r->options = *options; r->constants = system; xe_dxil_alu_init(&r->alu,m);
  for (i = 0; i < 4; ++i) r->options.formats[i] = normalize_format(options->formats[i]);
  if (xe_dxil_shader_ordered_buffer(shader,0,2) != 0 ||
      xe_dxil_shader_resource(shader,XE_DXIL_RESOURCE_UAV,DXIL_RESOURCE_KIND_RAW_BUFFER,0,1,1,0) != 1) goto fail;
  r->edram = xe_dxil_resource_handle(&r->alu,XE_DXIL_RESOURCE_UAV,0,2);
  r->counters = xe_dxil_resource_handle(&r->alu,XE_DXIL_RESOURCE_UAV,1,1);
  r->surviving = dxil_emit_alloca(m,dxil_module_get_int_type(m,32),ic(r,1),4);
  if (!r->surviving || !dxil_emit_store(m,ic(r,0),r->surviving,4,0)) goto fail;
  for (i = 0; i < (o->full_counters ? 2u : 0u); ++i) {
    r->failed[i] = dxil_emit_alloca(m,dxil_module_get_int_type(m,32),ic(r,1),4);
    if (!r->failed[i] || !dxil_emit_store(m,ic(r,0),r->failed[i],4,0)) goto fail;
  }
  if (!r->alu.failed) return r;
fail:
  free(r); return NULL;
}
void xe_dxil_guest_rop_destroy(XeDxilGuestRop *rop) { free(rop); }
static XeDxilValue expand(Rop *r, XeDxilValue v) {
  return bin(r,DXIL_BINOP_OR,bin(r,DXIL_BINOP_SHL,bin(r,DXIL_BINOP_LSHR,v,ic(r,1)),ic(r,2)),bin(r,DXIL_BINOP_AND,v,ic(r,1)));
}
static void addresses(Rop *r, XeDxilValue position[3], XeDxilValue offsets[3]) {
  const XeDxilRovDepthOptions *o = &r->options.depth;
  XeDxilValue x, y, gx, gy, sx, sy, u, v, half, tile_y, row, base;
  unsigned width = 80*o->scale_x, height = 16*o->scale_y;
  x = cast(r,DXIL_CAST_FPTOUI,0,position[0]); y = cast(r,DXIL_CAST_FPTOUI,0,position[1]);
  gx = bin(r,DXIL_BINOP_UDIV,x,ic(r,o->scale_x)); sx = bin(r,DXIL_BINOP_UREM,x,ic(r,o->scale_x));
  gy = bin(r,DXIL_BINOP_UDIV,y,ic(r,o->scale_y)); sy = bin(r,DXIL_BINOP_UREM,y,ic(r,o->scale_y));
  u = o->msaa == 2 ? expand(r,gx) : o->msaa == 1 ? bin(r,DXIL_BINOP_AND,gx,ic(r,~2u)) : gx;
  v = o->msaa ? expand(r,gy) : gy;
  if (o->msaa == 1) v = bin(r,DXIL_BINOP_OR,v,bin(r,DXIL_BINOP_AND,gx,ic(r,2)));
  u = bin(r,DXIL_BINOP_ADD,bin(r,DXIL_BINOP_MUL,u,ic(r,o->scale_x)),sx);
  v = bin(r,DXIL_BINOP_ADD,bin(r,DXIL_BINOP_MUL,v,ic(r,o->scale_y)),sy);
  half = bin(r,DXIL_BINOP_UDIV,u,ic(r,width/2)); tile_y = bin(r,DXIL_BINOP_UDIV,v,ic(r,height));
  row = bin(r,DXIL_BINOP_MUL,bin(r,DXIL_BINOP_UREM,v,ic(r,height)),ic(r,width));
  base = bin(r,DXIL_BINOP_ADD,bin(r,DXIL_BINOP_MUL,tile_y,word(r,o->pitch_word)),
    bin(r,DXIL_BINOP_ADD,bin(r,DXIL_BINOP_MUL,bin(r,DXIL_BINOP_LSHR,half,ic(r,1)),ic(r,width*height)),
      bin(r,DXIL_BINOP_ADD,row,bin(r,DXIL_BINOP_UREM,u,ic(r,width/2)))));
  offsets[0] = bin(r,DXIL_BINOP_ADD,base,pick(r,flag(r,half,1),ic(r,width/2),ic(r,0)));
  offsets[1] = bin(r,DXIL_BINOP_ADD,bin(r,DXIL_BINOP_MUL,bin(r,DXIL_BINOP_MUL,tile_y,word(r,o->pitch_word)),ic(r,2)),
    bin(r,DXIL_BINOP_ADD,bin(r,DXIL_BINOP_MUL,half,ic(r,width*height)),bin(r,DXIL_BINOP_ADD,row,bin(r,DXIL_BINOP_MUL,bin(r,DXIL_BINOP_UREM,u,ic(r,width/2)),ic(r,2)))));
  offsets[2] = bin(r,DXIL_BINOP_ADD,base,pick(r,flag(r,half,1),ic(r,0),ic(r,width/2)));
}
static XeDxilValue sample_address(Rop *r, XeDxilValue base, unsigned sample, unsigned wide) {
  const XeDxilRovDepthOptions *o = &r->options.depth;
  unsigned offset = 2*o->scale_x*(wide ? 2 : 1)*(sample&1)+2*o->scale_y*(80*o->scale_x)*(sample>>1);
  return bin(r,DXIL_BINOP_MUL,bin(r,DXIL_BINOP_UREM,bin(r,DXIL_BINOP_ADD,base,ic(r,offset)),ic(r,80*16*2048*o->scale_x*o->scale_y)),ic(r,4));
}
static int add_counter(Rop *r, XeDxilValue index, unsigned lane, XeDxilValue count) {
  XeDxilValue args[7]; unsigned done;
  if (!conditional(r,cmp(r,DXIL_ICMP_NE,count,ic(r,0)),&done)) return 0;
  args[0] = ic(r,78); args[1] = r->counters; args[2] = ic(r,0);
  args[3] = bin(r,DXIL_BINOP_ADD,index,ic(r,lane*4)); args[4] = ic(r,0); args[5] = args[4]; args[6] = count;
  return call(r,"dx.op.atomicBinOp",DXIL_I32,args,7) && end(r,done);
}
static int depth_test(Rop *r, XeDxilValue position[3], XeDxilValue front, XeDxilValue flags, XeDxilValue coverage, XeDxilValue commit,
    XeDxilValue depth, XeDxilValue dx, XeDxilValue dy, XeDxilValue base) {
  static const float sample_x[4] = {-0.125f,0.375f,-0.375f,0.125f};
  static const float sample_y[4] = {-0.375f,-0.125f,0.125f,0.375f};
  const XeDxilRovDepthOptions *o = &r->options.depth;
  XeDxilDepthStencilState state; XeDxilDepthStencilResult tested;
  XeDxilValue center, slope, offset, scale, masks, ops, incoming, packed, previous, address, value;
  unsigned i, j, enabled_done, sample_done, write_done, host_sample;
  int explicit_depth = depth != NULL;
  if (!dxil_emit_store(r->alu.module,coverage,r->surviving,4,0)) return 0;
  if (!conditional(r,flag(r,flags,o->depth_stencil_mask),&enabled_done)) return 0;
  center = depth;
  if (!explicit_depth) {
    slope = binary_float(r,35,unary(r,6,dx),unary(r,6,dy));
    scale = pick(r,front,fw(r,o->front_scale_word),fw(r,o->back_scale_word));
    offset = pick(r,front,fw(r,o->front_offset_word),fw(r,o->back_offset_word));
    center = bin(r,DXIL_BINOP_ADD,position[2],bin(r,DXIL_BINOP_ADD,bin(r,DXIL_BINOP_MUL,slope,scale),offset));
  }
  masks = pick(r,front,word(r,o->front_masks_word),word(r,o->back_masks_word));
  ops = pick(r,front,word(r,o->front_ops_word),word(r,o->back_ops_word));
  state.depth_enabled = ic(r,1); state.depth_write = cast(r,DXIL_CAST_ZEXT,0,flag(r,flags,o->depth_write_mask));
  state.depth_function = field(r,flags,o->depth_function_shift,7);
  state.stencil_enabled = cast(r,DXIL_CAST_ZEXT,0,flag(r,flags,o->stencil_mask));
  state.stencil_function = field(r,ops,0,7); state.reference = field(r,masks,0,255);
  state.read_mask = field(r,masks,8,255); state.write_mask = field(r,masks,16,255);
  state.stencil_fail = field(r,ops,3,7); state.depth_pass = field(r,ops,6,7); state.depth_fail = field(r,ops,9,7);
  for (i = 0; i < (1u<<o->msaa); ++i) {
    if (!conditional(r,flag(r,coverage,1u<<i),&sample_done)) return 0;
    host_sample = o->msaa == 1 && i == 1 ? 3 : i;
    incoming = center;
    if (!explicit_depth && o->msaa) incoming = bin(r,DXIL_BINOP_ADD,center,bin(r,DXIL_BINOP_ADD,
      bin(r,DXIL_BINOP_MUL,dx,fc(r,sample_x[host_sample])),bin(r,DXIL_BINOP_MUL,dy,fc(r,sample_y[host_sample]))));
    incoming = xe_dxil_alu_saturate(&r->alu,incoming);
    packed = quantize(r,flag(r,flags,o->float24_mask),incoming);
    address = sample_address(r,base,i,0); previous = xe_dxil_raw_buffer_load(&r->alu,r->edram,address);
    if (!xe_dxil_edram_depth_stencil(&r->alu,&state,previous,packed,&tested) ||
        !conditional(r,bin(r,DXIL_BINOP_AND,tested.changed,flag(r,commit,1u<<i)),&write_done) || !xe_dxil_raw_buffer_store(&r->alu,r->edram,address,tested.packed) || !end(r,write_done)) return 0;
    value = dxil_emit_load(r->alu.module,r->surviving,4,0);
    value = pick(r,tested.passed,value,bin(r,DXIL_BINOP_AND,value,ic(r,~(1u<<i))));
    if (!value || !dxil_emit_store(r->alu.module,value,r->surviving,4,0)) return 0;
    if (o->full_counters) for (j = 0; j < 2; ++j) {
      value = cast(r,DXIL_CAST_ZEXT,0,j ? tested.stencil_failed : tested.depth_failed);
      value = bin(r,DXIL_BINOP_ADD,dxil_emit_load(r->alu.module,r->failed[j],4,0),value);
      if (!value || !dxil_emit_store(r->alu.module,value,r->failed[j],4,0)) return 0;
    }
    if (!end(r,sample_done)) return 0;
  }
  return end(r,enabled_done);
}
static int store_changed(Rop *r, XeDxilValue address, unsigned words,
    XeDxilValue packed[2], XeDxilValue previous[2], XeDxilValue keep[2]) {
  XeDxilValue merged[2], changed = ic(r,0);
  unsigned j, done;
  for (j = 0; j < words; ++j) {
    merged[j] = bin(r,DXIL_BINOP_OR,bin(r,DXIL_BINOP_AND,previous[j],keep[j]),
      bin(r,DXIL_BINOP_AND,packed[j],bin(r,DXIL_BINOP_XOR,keep[j],ic(r,UINT32_MAX))));
    changed = bin(r,DXIL_BINOP_OR,changed,bin(r,DXIL_BINOP_XOR,merged[j],previous[j]));
  }
  if (!conditional(r,cmp(r,DXIL_ICMP_NE,changed,ic(r,0)),&done)) return 0;
  for (j = 0; j < words; ++j)
    if (!xe_dxil_raw_buffer_store(&r->alu,r->edram,bin(r,DXIL_BINOP_ADD,address,ic(r,j*4)),merged[j])) return 0;
  return end(r,done);
}
static int overwrite_samples(Rop *r, XeDxilValue base, unsigned words, XeDxilValue packed[2],
    XeDxilValue keep[2], XeDxilValue coverage, unsigned merging) {
  XeDxilValue address, previous[2];
  unsigned i, j, sample_done;
  for (i = 0; i < (1u<<r->options.depth.msaa); ++i) {
    if (!conditional(r,flag(r,coverage,1u<<i),&sample_done)) return 0;
    address = sample_address(r,base,i,words == 2);
    if (merging) {
      for (j = 0; j < words; ++j) previous[j] = xe_dxil_raw_buffer_load(&r->alu,r->edram,bin(r,DXIL_BINOP_ADD,address,ic(r,j*4)));
      if (!store_changed(r,address,words,packed,previous,keep)) return 0;
    } else for (j = 0; j < words; ++j)
      if (!xe_dxil_raw_buffer_store(&r->alu,r->edram,bin(r,DXIL_BINOP_ADD,address,ic(r,j*4)),packed[j])) return 0;
    if (!end(r,sample_done)) return 0;
  }
  return !r->alu.failed;
}
static int overwrite(Rop *r, unsigned format, XeDxilValue base, XeDxilValue color[4], XeDxilValue keep[2], XeDxilValue coverage) {
  XeDxilValue packed[2], any_keep;
  unsigned blocks, words = format_words(format);
  if (!xe_dxil_pack_render_color(&r->alu,format,color,packed)) return 0;
  any_keep = words == 2 ? bin(r,DXIL_BINOP_OR,keep[0],keep[1]) : keep[0];
  if (!dxil_function_reserve_blocks(r->alu.module,3,&blocks) ||
      !dxil_emit_branch(r->alu.module,cmp(r,DXIL_ICMP_NE,any_keep,ic(r,0)),blocks,blocks+1)) return 0;
  begin(r,blocks);
  if (!overwrite_samples(r,base,words,packed,keep,coverage,1) || !end(r,blocks+2)) return 0;
  begin(r,blocks+1);
  if (!overwrite_samples(r,base,words,packed,keep,coverage,0) || !end(r,blocks+2)) return 0;
  return !r->alu.failed;
}
static int blend_samples(Rop *r, unsigned rt, unsigned format, XeDxilValue base,
    XeDxilValue color[4], XeDxilValue keep[2], XeDxilValue coverage, XeDxilValue controls) {
  XeDxilValue source[4], constant[4], destination[4], result[4], low[4], high[4], packed[2], previous[2], address;
  unsigned i, j, sample_done, words = format_words(format), components = format_components(format), fixed;
  for (j = 0; j < 4; ++j) {
    low[j] = fw(r,r->options.clamp_word+rt*4+(j == 3 ? 1 : 0));
    high[j] = fw(r,r->options.clamp_word+rt*4+(j == 3 ? 3 : 2));
    fixed = fixed_component(format,j);
    source[j] = clamp_blend(r,color[j],fixed,low[j],high[j]);
    constant[j] = clamp_blend(r,fw(r,r->options.blend_constant_word+j),fixed,low[j],high[j]);
  }
  for (i = 0; i < (1u<<r->options.depth.msaa); ++i) {
    if (!conditional(r,flag(r,coverage,1u<<i),&sample_done)) return 0;
    address = sample_address(r,base,i,words == 2);
    previous[1] = ic(r,0);
    for (j = 0; j < words; ++j) previous[j] = xe_dxil_raw_buffer_load(&r->alu,r->edram,bin(r,DXIL_BINOP_ADD,address,ic(r,j*4)));
    if (!xe_dxil_unpack_render_color(&r->alu,format,previous,destination)) return 0;
    for (j = 0; j < 4; ++j) result[j] = fc(r,j == 3 ? 1.0f : 0.0f);
    for (j = 0; j < components; ++j)
      result[j] = blend_component(r,source,destination,constant,j,fixed_component(format,j),low[j],high[j],controls);
    if (!xe_dxil_pack_render_color(&r->alu,format,result,packed) || !store_changed(r,address,words,packed,previous,keep) || !end(r,sample_done)) return 0;
  }
  return !r->alu.failed;
}
int xe_dxil_guest_rop_emit(XeDxilGuestRop *rop, XeDxilValue position[3],
    XeDxilValue front, XeDxilValue flags, XeDxilValue coverage, XeDxilValue commit_coverage, XeDxilValue depth,
    XeDxilValue depth_dx, XeDxilValue depth_dy, XeDxilValue written,
    XeDxilValue colors[4][4], unsigned *current_block) {
  Rop *r = rop;
  XeDxilValue offsets[3], base, index, count, args[2], passed, keep[2], writable, controls;
  unsigned i, words, color_done, rt_done, blend_blocks, counter_done;
  if (!r || r->alu.failed || !position || !position[0] || !position[1] || !position[2] || !front || !flags ||
      !coverage || !commit_coverage || !written || !current_block || (!depth && (!depth_dx || !depth_dy)) || (r->options.color_mask && !colors)) return 0;
  r->current_block = *current_block;
  if (r->options.depth.msaa == 1) {
    coverage = bin(r,DXIL_BINOP_OR,field(r,coverage,0,1),bin(r,DXIL_BINOP_SHL,field(r,coverage,3,1),ic(r,1)));
    commit_coverage = bin(r,DXIL_BINOP_OR,field(r,commit_coverage,0,1),bin(r,DXIL_BINOP_SHL,field(r,commit_coverage,3,1),ic(r,1)));
  }
  coverage = bin(r,DXIL_BINOP_AND,coverage,ic(r,(1u<<(1u<<r->options.depth.msaa))-1));
  commit_coverage = bin(r,DXIL_BINOP_AND,commit_coverage,coverage);
  if (!r->options.early_depth) coverage = commit_coverage;
  addresses(r,position,offsets);
  base = bin(r,DXIL_BINOP_ADD,word(r,r->options.depth.depth_base_word),offsets[2]);
  if (!depth_test(r,position,front,flags,coverage,commit_coverage,depth,depth_dx,depth_dy,base)) return 0;
  passed = bin(r,DXIL_BINOP_AND,dxil_emit_load(r->alu.module,r->surviving,4,0),commit_coverage);
  index = word(r,r->options.depth.counter_word);
  if (!conditional(r,cmp(r,DXIL_ICMP_NE,index,ic(r,UINT32_MAX)),&counter_done)) return 0;
  index = bin(r,DXIL_BINOP_MUL,index,ic(r,16));
  args[0] = ic(r,31); args[1] = passed; count = call(r,"dx.op.unaryBits",DXIL_I32,args,2);
  if (!add_counter(r,index,2,count)) return 0;
  if (r->options.depth.full_counters) for (i = 0; i < 2; ++i)
    if (!add_counter(r,index,i ? 3 : 1,dxil_emit_load(r->alu.module,r->failed[i],4,0))) return 0;
  if (!end(r,counter_done)) return 0;
  if (r->options.color_mask) {
    if (!conditional(r,cmp(r,DXIL_ICMP_NE,passed,ic(r,0)),&color_done)) return 0;
    for (i = 0; i < 4; ++i) if (r->options.color_mask & (1u<<i)) {
      unsigned format = r->options.formats[i];
      words = format_words(format); keep[0] = word(r,r->options.keep_word+i*2);
      keep[1] = words == 2 ? word(r,r->options.keep_word+i*2+1) : ic(r,0);
      writable = cmp(r,DXIL_ICMP_NE,words == 2 ? bin(r,DXIL_BINOP_AND,keep[0],keep[1]) : keep[0],ic(r,UINT32_MAX));
      if (!conditional(r,bin(r,DXIL_BINOP_AND,writable,flag(r,written,1u<<i)),&rt_done)) return 0;
      base = bin(r,DXIL_BINOP_ADD,word(r,r->options.base_word+i),offsets[words == 2 ? 1 : 0]);
      if (r->options.no_blending) {
        if (!overwrite(r,format,base,colors[i],keep,passed)) return 0;
      } else {
        controls = word(r,r->options.blend_word+i);
        if (!dxil_function_reserve_blocks(r->alu.module,3,&blend_blocks) ||
            !dxil_emit_branch(r->alu.module,cmp(r,DXIL_ICMP_NE,controls,ic(r,0x00010001)),blend_blocks,blend_blocks+1)) return 0;
        begin(r,blend_blocks);
        if (!blend_samples(r,i,format,base,colors[i],keep,passed,controls) || !end(r,blend_blocks+2)) return 0;
        begin(r,blend_blocks+1);
        if (!overwrite(r,format,base,colors[i],keep,passed) || !end(r,blend_blocks+2)) return 0;
      }
      if (!end(r,rt_done)) return 0;
    }
    if (!end(r,color_done)) return 0;
  }
  *current_block = r->current_block; return !r->alu.failed;
}
