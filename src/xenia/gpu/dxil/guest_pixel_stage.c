/* Native guest pixel stage. MSVC C89; BSD license. */
#include "stage_layout.h"
#include "guest_pixel_stage.h"
#include "edram_format.h"
#include <dxil_module.h>
#include <dxil_function.h>
typedef struct Pixel {
  XeDxilShaderModule *shader;
  XeDxilGuestRop *rop;
  XeDxilAlu alu;
  XeDxilValue system, killed, written;
  unsigned current_block, saw_kill;
} Pixel;
static XeDxilValue ic(Pixel *p, uint32_t v) { return dxil_module_get_int32_const(p->alu.module,(int32_t)v); }
static XeDxilValue fc(Pixel *p, float v) { return dxil_module_get_float_const(p->alu.module,v); }
static XeDxilValue bin(Pixel *p, enum dxil_bin_opcode op, XeDxilValue a, XeDxilValue b) {
  XeDxilValue v = a && b ? dxil_emit_binop(p->alu.module,op,a,b,0) : NULL;
  if (!v) p->alu.failed = 1;
  return v;
}
static XeDxilValue cmp(Pixel *p, enum dxil_cmp_pred op, XeDxilValue a, XeDxilValue b) {
  XeDxilValue v = a && b ? dxil_emit_cmp(p->alu.module,op,a,b) : NULL;
  if (!v) p->alu.failed = 1;
  return v;
}
static XeDxilValue pick(Pixel *p, XeDxilValue c, XeDxilValue a, XeDxilValue b) {
  XeDxilValue v = c && a && b ? dxil_emit_select(p->alu.module,c,a,b) : NULL;
  if (!v) p->alu.failed = 1;
  return v;
}
static XeDxilValue cast(Pixel *p, enum dxil_cast_opcode op, unsigned floating, XeDxilValue v) {
  v = v ? dxil_emit_cast(p->alu.module,op,floating ? dxil_module_get_float_type(p->alu.module,32) : dxil_module_get_int_type(p->alu.module,32),v) : NULL;
  if (!v) p->alu.failed = 1;
  return v;
}
static XeDxilValue call(Pixel *p, const char *name, enum overload_type type, XeDxilValue *args, unsigned n) {
  const struct dxil_func *f = dxil_get_function(p->alu.module,name,type);
  unsigned i;
  for (i = 0; i < n; ++i) if (!args[i]) { p->alu.failed = 1; return NULL; }
  return f ? dxil_emit_call(p->alu.module,f,args,n) : NULL;
}
static XeDxilValue unary(Pixel *p, unsigned opcode, XeDxilValue value) {
  XeDxilValue args[2]; args[0] = ic(p,opcode); args[1] = value;
  return call(p,"dx.op.unary",DXIL_F32,args,2);
}
static XeDxilValue word(Pixel *p, unsigned index) { return xe_dxil_cbuffer_word(&p->alu,p->system,ic(p,index)); }
static XeDxilValue fw(Pixel *p, unsigned index) { return cast(p,DXIL_CAST_BITCAST,1,word(p,index)); }
static XeDxilValue field(Pixel *p, XeDxilValue value, unsigned shift, unsigned mask) {
  return bin(p,DXIL_BINOP_AND,bin(p,DXIL_BINOP_LSHR,value,ic(p,shift)),ic(p,mask));
}
static XeDxilValue flag(Pixel *p, XeDxilValue value, unsigned mask) {
  return cmp(p,DXIL_ICMP_NE,bin(p,DXIL_BINOP_AND,value,ic(p,mask)),ic(p,0));
}
static XeDxilValue input(Pixel *p, unsigned id, unsigned component, unsigned integer) {
  XeDxilValue args[5];
  args[0] = ic(p,4); args[1] = ic(p,id); args[2] = ic(p,0);
  args[3] = dxil_module_get_int8_const(p->alu.module,(int8_t)component);
  args[4] = dxil_module_get_undef(p->alu.module,dxil_module_get_int_type(p->alu.module,32));
  return call(p,"dx.op.loadInput",integer ? DXIL_I32 : DXIL_F32,args,5);
}
static XeDxilValue attribute(Pixel *p, unsigned id, unsigned component, unsigned vertex) {
  XeDxilValue args[5];
  args[0] = ic(p,137); args[1] = ic(p,id); args[2] = ic(p,0);
  args[3] = dxil_module_get_int8_const(p->alu.module,(int8_t)component);
  args[4] = dxil_module_get_int8_const(p->alu.module,(int8_t)vertex);
  return call(p,"dx.op.attributeAtVertex",DXIL_F32,args,5);
}
static int output(Pixel *p, unsigned id, unsigned component, unsigned integer, XeDxilValue value) {
  XeDxilValue args[5];
  const struct dxil_func *f = dxil_get_function(p->alu.module,"dx.op.storeOutput",integer ? DXIL_I32 : DXIL_F32);
  args[0] = ic(p,5); args[1] = ic(p,id); args[2] = ic(p,0);
  args[3] = dxil_module_get_int8_const(p->alu.module,(int8_t)component); args[4] = value;
  return f && value && dxil_emit_call_void(p->alu.module,f,args,5);
}
static int discard(Pixel *p, XeDxilValue condition) {
  XeDxilValue args[2];
  const struct dxil_func *f;
  /* Ordered EDRAM must retain early depth/stencil failure counts even when
   * the guest later kills a pixel. Defer the kill to the commit mask. */
  if (p->rop) {
    args[0] = dxil_emit_load(p->alu.module,p->killed,4,0);
    args[1] = bin(p,DXIL_BINOP_OR,args[0],cast(p,DXIL_CAST_ZEXT,0,condition));
    return args[1] && dxil_emit_store(p->alu.module,args[1],p->killed,4,0);
  }
  f = dxil_get_function(p->alu.module,"dx.op.discard",DXIL_NONE);
  args[0] = ic(p,82); args[1] = condition;
  return f && condition && dxil_emit_call_void(p->alu.module,f,args,2);
}
static int kill(void *context, XeDxilAlu *alu, XeDxilAluIo *io, XeDxilValue condition) {
  Pixel *p = (Pixel *)context;
  XeDxilValue value = dxil_emit_load(alu->module,p->killed,4,0);
  (void)io; p->saw_kill = 1;
  value = bin(p,DXIL_BINOP_OR,value,cast(p,DXIL_CAST_ZEXT,0,condition));
  return value && dxil_emit_store(alu->module,value,p->killed,4,0);
}
static int written(void *context, XeDxilGuestProgram *program, unsigned index, unsigned mask) {
  Pixel *p = (Pixel *)context;
  (void)program;
  if (index >= 4 || !mask) return 1;
  if (p->rop) return dxil_emit_store(p->alu.module,
    bin(p,DXIL_BINOP_OR,dxil_emit_load(p->alu.module,p->written,4,0),ic(p,1u<<index)),p->written,4,0);
  return index ? 1 : dxil_emit_store(p->alu.module,ic(p,1),p->written,4,0);
}
static int factor_valid(unsigned factor) { return factor <= XE_DXIL_BLEND_ONE || (factor >= XE_DXIL_BLEND_SOURCE_COLOR && factor <= XE_DXIL_BLEND_INVERSE_SOURCE_ALPHA) || (factor >= XE_DXIL_BLEND_CONSTANT_COLOR && factor <= XE_DXIL_BLEND_INVERSE_CONSTANT_ALPHA); }
static XeDxilValue factor(Pixel *p, const XeDxilGuestPixelOptions *o, unsigned f, unsigned component, XeDxilValue color[4]) {
  XeDxilValue value;
  if (f < 2) return fc(p,(float)f);
  if (f == XE_DXIL_BLEND_SOURCE_COLOR || f == XE_DXIL_BLEND_INVERSE_SOURCE_COLOR) value = color[component];
  else if (f == 6 || f == 7) value = color[3];
  else value = fw(p,o->blend_constant_word+((f == 14 || f == 15) ? 3 : component));
  return (f & 1) ? bin(p,DXIL_BINOP_SUB,fc(p,1),value) : value;
}
static XeDxilValue alpha_pass(Pixel *p, XeDxilValue function, XeDxilValue alpha, XeDxilValue reference) {
  XeDxilValue pass = bin(p,DXIL_BINOP_OR,
    bin(p,DXIL_BINOP_AND,flag(p,function,1),cmp(p,DXIL_FCMP_OLT,alpha,reference)),
    bin(p,DXIL_BINOP_OR,bin(p,DXIL_BINOP_AND,flag(p,function,2),cmp(p,DXIL_FCMP_OEQ,alpha,reference)),
      bin(p,DXIL_BINOP_AND,flag(p,function,4),cmp(p,DXIL_FCMP_OGT,alpha,reference))));
  pass = pick(p,cmp(p,DXIL_ICMP_EQ,function,ic(p,5)),cmp(p,DXIL_FCMP_UNE,alpha,reference),pass);
  return bin(p,DXIL_BINOP_OR,cmp(p,DXIL_ICMP_EQ,function,ic(p,7)),pass);
}
static void begin(Pixel *p, unsigned block) {
  p->alu.module->cur_emitting_func->basic_block_ids[block] = (int)p->alu.module->cur_emitting_func->curr_block;
  p->current_block = block;
}
static XeDxilValue sample_mask(Pixel *p, const XeDxilGuestPixelOptions *o, XeDxilValue flags,
    XeDxilValue alpha, XeDxilValue position[3], XeDxilValue live) {
  static const float bases[3][4] = {{1,0,0,0},{0.5f,1,0,0},{0.75f,0.25f,0.5f,1}};
  XeDxilValue state = word(p,o->alpha_to_mask_word), offset, index, msaa, mask, incoming[4], threshold;
  struct dxil_instr *phi;
  unsigned blocks, mode, i, bit, predecessors[4];
  predecessors[0] = p->current_block; incoming[0] = ic(p,UINT32_MAX);
  if (!dxil_function_reserve_blocks(p->alu.module,6,&blocks) ||
      !dxil_emit_branch(p->alu.module,bin(p,DXIL_BINOP_AND,live,cmp(p,DXIL_ICMP_NE,state,ic(p,0))),blocks,blocks+5)) return NULL;
  begin(p,blocks);
  index = bin(p,DXIL_BINOP_OR,bin(p,DXIL_BINOP_SHL,bin(p,DXIL_BINOP_AND,cast(p,DXIL_CAST_FPTOUI,0,position[0]),ic(p,1)),ic(p,1)),
    bin(p,DXIL_BINOP_AND,cast(p,DXIL_CAST_FPTOUI,0,position[1]),ic(p,1)));
  offset = cast(p,DXIL_CAST_UITOFP,1,bin(p,DXIL_BINOP_AND,bin(p,DXIL_BINOP_LSHR,state,bin(p,DXIL_BINOP_SHL,index,ic(p,1))),ic(p,3)));
  msaa = field(p,flags,o->msaa_shift,3);
  if (!dxil_emit_branch(p->alu.module,cmp(p,DXIL_ICMP_EQ,msaa,ic(p,0)),blocks+2,blocks+1)) return NULL;
  begin(p,blocks+1);
  if (!dxil_emit_branch(p->alu.module,cmp(p,DXIL_ICMP_EQ,msaa,ic(p,2)),blocks+4,blocks+3)) return NULL;
  for (mode = 0; mode < 3; ++mode) {
    begin(p,blocks+2+mode); mask = ic(p,0);
    for (i = 0; i < (1u<<mode); ++i) {
      bit = mode == 1 ? (o->native_2x ? 1-i : i ? 3 : 0) : i;
      threshold = bin(p,DXIL_BINOP_SUB,fc(p,bases[mode][i]),bin(p,DXIL_BINOP_MUL,offset,fc(p,1.0f/(float)(4u<<mode))));
      mask = bin(p,DXIL_BINOP_OR,mask,pick(p,cmp(p,DXIL_FCMP_OGE,alpha,threshold),ic(p,1u<<bit),ic(p,0)));
    }
    incoming[mode+1] = mask; predecessors[mode+1] = p->current_block;
    if (!mask || !dxil_emit_branch(p->alu.module,NULL,blocks+5,0)) return NULL;
  }
  begin(p,blocks+5);
  phi = dxil_emit_phi(p->alu.module,dxil_module_get_int_type(p->alu.module,32));
  if (!phi || !dxil_phi_add_incoming(phi,incoming,predecessors,4)) return NULL;
  return dxil_instr_get_return_value(phi);
}
XeDxilShaderBinary *xe_dxil_build_guest_pixel_owned(const XeDxilGuestPixelOptions *o,
    const uint32_t *words, size_t word_count, unsigned cf_count) {
  Pixel p;
  XeDxilGuestProgramOptions options;
  XeDxilGuestProgram *program = NULL;
  XeDxilShaderBinary *binary = NULL;
  XeDxilValue rop_colors[4][4], values[4], color[4], position[3], flags, front, value, slope, dx, dy;
  XeDxilValue bary_y = NULL, bary_z = NULL, a, b, c, mask, live, alpha, coverage, args[7], counters = NULL, index;
  int bary_id = -1, colors[4], depth_id = -1, mask_id = -1, point_id = -1, input_id;
  unsigned i, j, packed = 0, cbv = 1, n, float24, truncate, alpha_blocks;
  const struct dxil_type *integer_type;
  if (!o || !words || o->interpolator_mask > 65535 || o->color_mask > 15 || o->depth_mode > 6 ||
      !o->scale_x || !o->scale_y || o->scale_x > 16 || o->scale_y > 16 || o->alpha_function_shift > 29 || o->msaa_shift > 30 ||
      !factor_valid(o->blend_rgb_factor) || !factor_valid(o->blend_alpha_factor) || o->program.texture || o->program.allocation ||
      o->system_constants_bytes < 16 || o->system_constants_bytes > 65536 ||
      (o->rop && (o->rop->color_mask != o->color_mask || o->rop->depth.system_constants_bytes != o->system_constants_bytes || o->rop->depth.flags_word != o->flags_word))) return NULL;
  n = o->system_constants_bytes/4;
  if (o->flags_word >= n || o->alpha_reference_word >= n || o->alpha_to_mask_word >= n || o->color_bias_word > n-4 ||
      o->blend_constant_word > n-4 || o->front_scale_word >= n || o->back_scale_word >= n || o->front_offset_word >= n ||
      o->back_offset_word >= n || (o->count_coverage && (o->counter_word >= n || !o->counter_stride_bytes || (o->counter_stride_bytes & 3)))) return NULL;
  memset(&p,0,sizeof(p)); options = o->program; options.context = &p; options.kill = kill; options.export_written = written;
  p.shader = xe_dxil_shader_create(DXIL_PIXEL_SHADER); if (!p.shader) return NULL;
  xe_dxil_alu_init(&p.alu,xe_dxil_shader_module(p.shader)); integer_type = dxil_module_get_int_type(p.alu.module,32);
  if (xe_dxil_shader_signature(p.shader,0,"SV_Position",0,DXIL_SEM_POSITION,DXIL_PROG_SEM_POSITION,XE_DXIL_GUEST_POSITION_ROW,0,4,DXIL_INTERP_LINEAR_NOPERSPECTIVE) != 0 ||
      xe_dxil_shader_signature_typed(p.shader,0,"SV_IsFrontFace",0,DXIL_SEM_IS_FRONT_FACE,DXIL_PROG_SEM_IS_FRONTFACE,XE_DXIL_GUEST_FRONT_FACE_ROW,0,1,DXIL_INTERP_CONSTANT,DXIL_COMP_TYPE_U32) != 1) goto done;
  for (i = 0; i < 16; ++i) if (o->interpolator_mask & (1u<<i)) {
    input_id = xe_dxil_shader_signature(p.shader,0,"TEXCOORD",packed,DXIL_SEM_ARBITRARY,0,packed,0,4,
      o->precise_interpolation ? DXIL_INTERP_CONSTANT : (o->centroid_mask & (1u<<i)) ? DXIL_INTERP_LINEAR_CENTROID : DXIL_INTERP_LINEAR);
    if (input_id < 0) goto done;
    ++packed;
  }
  if (o->precise_interpolation && o->interpolator_mask) {
    bary_id = xe_dxil_shader_barycentrics(p.shader); if (bary_id < 0) goto done;
  }
  if (o->param_gen && o->param_point) {
    point_id = xe_dxil_shader_signature(p.shader,0,"TEXCOORD",packed,DXIL_SEM_ARBITRARY,0,packed,0,2,DXIL_INTERP_LINEAR_NOPERSPECTIVE);
    if (point_id < 0) goto done;
  }
  for (i = 0; i < 4; ++i) {
    colors[i] = -1;
    if (!o->rop && (o->color_mask & (1u<<i))) {
      colors[i] = xe_dxil_shader_signature(p.shader,1,"SV_Target",i,DXIL_SEM_TARGET,DXIL_PROG_SEM_TARGET,i,0,4,0);
      if (colors[i] < 0) goto done;
    }
  }
  float24 = o->depth_mode == 2 || o->depth_mode == 3 || o->depth_mode == 5 || o->depth_mode == 6;
  truncate = o->depth_mode == 2 || o->depth_mode == 5;
  if (!o->rop && (o->writes_depth || o->depth_mode >= 2)) {
    depth_id = xe_dxil_shader_signature(p.shader,1,o->depth_mode == 2 && !o->writes_depth ? "SV_DepthLessEqual" : "SV_Depth",0,
      o->depth_mode == 2 && !o->writes_depth ? DXIL_SEM_DEPTH_LE : DXIL_SEM_DEPTH,
      o->depth_mode == 2 && !o->writes_depth ? DXIL_PROG_SEM_DEPTH_LE : DXIL_PROG_SEM_DEPTH,UINT32_MAX,0,1,0);
    if (depth_id < 0 || (!o->writes_depth && !xe_dxil_shader_sample_frequency(p.shader))) goto done;
  }
  if (!o->rop && (o->color_mask & 1)) {
    mask_id = xe_dxil_shader_signature_typed(p.shader,1,"SV_Coverage",0,DXIL_SEM_COVERAGE,DXIL_PROG_SEM_COVERAGE,UINT32_MAX,0,1,0,DXIL_COMP_TYPE_U32);
    if (mask_id < 0) goto done;
  }
  if (xe_dxil_shader_resource(p.shader,XE_DXIL_RESOURCE_CBV,DXIL_RESOURCE_KIND_CBUFFER,1,0,1,o->system_constants_bytes) != 0) goto done;
  p.system = xe_dxil_resource_handle(&p.alu,XE_DXIL_RESOURCE_CBV,0,0);
  options.float_constants = NULL;
  if (options.float_constant_count) {
    if (xe_dxil_shader_resource(p.shader,XE_DXIL_RESOURCE_CBV,DXIL_RESOURCE_KIND_CBUFFER,1,2,1,options.float_constant_count*16) != (int)cbv) goto done;
    options.float_constants = xe_dxil_resource_handle(&p.alu,XE_DXIL_RESOURCE_CBV,cbv++,2);
  }
  if (xe_dxil_shader_resource(p.shader,XE_DXIL_RESOURCE_CBV,DXIL_RESOURCE_KIND_CBUFFER,1,3,1,160) != (int)cbv) goto done;
  options.bool_loop_constants = xe_dxil_resource_handle(&p.alu,XE_DXIL_RESOURCE_CBV,cbv++,3);
  if (xe_dxil_shader_resource(p.shader,XE_DXIL_RESOURCE_CBV,DXIL_RESOURCE_KIND_CBUFFER,1,4,1,768) != (int)cbv ||
      xe_dxil_shader_resource(p.shader,XE_DXIL_RESOURCE_SRV,DXIL_RESOURCE_KIND_RAW_BUFFER,0,0,1,0) != 0) goto done;
  options.fetch_constants = xe_dxil_resource_handle(&p.alu,XE_DXIL_RESOURCE_CBV,cbv,4);
  options.shared_memory = xe_dxil_resource_handle(&p.alu,XE_DXIL_RESOURCE_SRV,0,0);
  if (o->rop) {
    p.rop = xe_dxil_guest_rop_create(p.shader,p.system,o->rop); if (!p.rop) goto done;
  } else if (o->count_coverage) {
    if (xe_dxil_shader_resource(p.shader,XE_DXIL_RESOURCE_UAV,DXIL_RESOURCE_KIND_RAW_BUFFER,0,1,1,0) != 0) goto done;
    counters = xe_dxil_resource_handle(&p.alu,XE_DXIL_RESOURCE_UAV,0,1);
  }
  p.killed = dxil_emit_alloca(p.alu.module,integer_type,ic(&p,1),4);
  p.written = dxil_emit_alloca(p.alu.module,integer_type,ic(&p,1),4);
  if (!p.killed || !p.written || !dxil_emit_store(p.alu.module,ic(&p,0),p.killed,4,0) || !dxil_emit_store(p.alu.module,ic(&p,0),p.written,4,0)) goto done;
  program = xe_dxil_guest_program_create(p.alu.module,&options); if (!program) goto done;
  flags = word(&p,o->flags_word); front = cmp(&p,DXIL_ICMP_NE,input(&p,1,0,1),ic(&p,0));
  for (i = 0; i < 3; ++i) position[i] = input(&p,0,i,0);
  dx = dy = NULL;
  if (!o->writes_depth && (o->rop || o->depth_mode >= 4)) { dx = unary(&p,83,position[2]); dy = unary(&p,84,position[2]); }
  if (bary_id >= 0) { bary_y = input(&p,(unsigned)bary_id,1,0); bary_z = input(&p,(unsigned)bary_id,2,0); }
  packed = 0;
  for (i = 0; i < 16; ++i) if (o->interpolator_mask & (1u<<i)) {
    for (j = 0; j < 4; ++j) {
      if (bary_id >= 0) {
        a = attribute(&p,2+packed,j,0); b = attribute(&p,2+packed,j,1); c = attribute(&p,2+packed,j,2);
        values[j] = bin(&p,DXIL_BINOP_ADD,bin(&p,DXIL_BINOP_ADD,a,bin(&p,DXIL_BINOP_MUL,bin(&p,DXIL_BINOP_SUB,b,a),bary_y)),
          bin(&p,DXIL_BINOP_MUL,bin(&p,DXIL_BINOP_SUB,c,a),bary_z));
      } else values[j] = input(&p,2+packed,j,0);
    }
    if (!xe_dxil_guest_program_register(program,i,15,values)) goto done;
    ++packed;
  }
  if (o->param_gen && o->param_register < options.register_count) {
    for (i = 0; i < 2; ++i) values[i] = bin(&p,DXIL_BINOP_MUL,unary(&p,6,unary(&p,27,position[i])),fc(&p,1.0f/(float)(i ? o->scale_y : o->scale_x)));
    if (o->param_point) {
      values[1] = cast(&p,DXIL_CAST_BITCAST,1,bin(&p,DXIL_BINOP_XOR,cast(&p,DXIL_CAST_BITCAST,0,values[1]),ic(&p,0x80000000)));
      values[2] = xe_dxil_alu_saturate(&p.alu,input(&p,(unsigned)point_id,0,0)); values[3] = xe_dxil_alu_saturate(&p.alu,input(&p,(unsigned)point_id,1,0));
    } else {
      values[0] = pick(&p,bin(&p,DXIL_BINOP_OR,front,cmp(&p,DXIL_ICMP_EQ,bin(&p,DXIL_BINOP_AND,flags,ic(&p,o->polygon_mask)),ic(&p,0))),values[0],
        cast(&p,DXIL_CAST_BITCAST,1,bin(&p,DXIL_BINOP_XOR,cast(&p,DXIL_CAST_BITCAST,0,values[0]),ic(&p,0x80000000))));
      values[2] = cast(&p,DXIL_CAST_BITCAST,1,pick(&p,flag(&p,flags,o->line_mask),ic(&p,0x80000000),ic(&p,0))); values[3] = fc(&p,0);
    }
    if (!xe_dxil_guest_program_register(program,o->param_register,15,values)) goto done;
  }
  if (!xe_dxil_guest_program_emit(program,words,word_count,cf_count)) goto done;
  p.current_block = *xe_dxil_guest_program_io(program)->current_block;
  if (p.saw_kill && !discard(&p,cmp(&p,DXIL_ICMP_NE,dxil_emit_load(p.alu.module,p.killed,4,0),ic(&p,0)))) goto done;
  mask = ic(&p,UINT32_MAX);
  if (o->color_mask & 1) {
    alpha = xe_dxil_guest_program_export(program,0,3); live = flag(&p,dxil_emit_load(p.alu.module,p.written,4,0),1);
    value = field(&p,flags,o->alpha_function_shift,7);
    if (!dxil_function_reserve_blocks(p.alu.module,2,&alpha_blocks) ||
        !dxil_emit_branch(p.alu.module,bin(&p,DXIL_BINOP_AND,live,cmp(&p,DXIL_ICMP_NE,value,ic(&p,7))),alpha_blocks,alpha_blocks+1)) goto done;
    begin(&p,alpha_blocks);
    value = alpha_pass(&p,value,alpha,fw(&p,o->alpha_reference_word));
    if (!discard(&p,cmp(&p,DXIL_ICMP_EQ,cast(&p,DXIL_CAST_ZEXT,0,value),ic(&p,0))) ||
        !dxil_emit_branch(p.alu.module,NULL,alpha_blocks+1,0)) goto done;
    begin(&p,alpha_blocks+1);
    mask = sample_mask(&p,o,flags,alpha,position,live);
    if ((!o->rop && !output(&p,(unsigned)mask_id,0,1,mask)) || !discard(&p,cmp(&p,DXIL_ICMP_EQ,mask,ic(&p,0)))) goto done;
  }
  if (p.rop) {
    for (i = 0; i < 4; ++i) for (j = 0; j < 4; ++j)
      rop_colors[i][j] = o->color_mask & (1u<<i) ?
        bin(&p,DXIL_BINOP_MUL,xe_dxil_guest_program_export(program,i,j),fw(&p,o->color_bias_word+i)) : fc(&p,0);
    value = o->writes_depth ? xe_dxil_alu_saturate(&p.alu,xe_dxil_guest_program_export(program,61,0)) : NULL;
    args[0] = ic(&p,91); coverage = call(&p,"dx.op.coverage",DXIL_I32,args,1);
    mask = pick(&p,cmp(&p,DXIL_ICMP_EQ,dxil_emit_load(p.alu.module,p.killed,4,0),ic(&p,0)),
      bin(&p,DXIL_BINOP_AND,coverage,mask),ic(&p,0));
    if (!xe_dxil_guest_rop_emit(p.rop,position,front,flags,coverage,mask,value,dx,dy,
          dxil_emit_load(p.alu.module,p.written,4,0),rop_colors,&p.current_block)) goto done;
    goto complete;
  }
  for (i = 0; i < 4; ++i) if (colors[i] >= 0) {
    for (j = 0; j < 4; ++j) {
      value = bin(&p,DXIL_BINOP_MUL,xe_dxil_guest_program_export(program,i,j),fw(&p,o->color_bias_word+i));
      color[j] = j == 3 ? value : pick(&p,flag(&p,flags,o->gamma_mask<<i),xe_dxil_gamma_encode(&p.alu,xe_dxil_alu_saturate(&p.alu,value)),value);
    }
    for (j = 0; j < 4; ++j) {
      value = i ? color[j] : bin(&p,DXIL_BINOP_MUL,color[j],factor(&p,o,j == 3 ? o->blend_alpha_factor : o->blend_rgb_factor,j,color));
      if (!output(&p,(unsigned)colors[i],j,0,value)) goto done;
    }
  }
  if (depth_id >= 0) {
    value = o->writes_depth ? xe_dxil_alu_saturate(&p.alu,xe_dxil_guest_program_export(program,61,0)) : position[2];
    if (!o->writes_depth && o->depth_mode >= 4) {
      slope = pick(&p,cmp(&p,DXIL_FCMP_OGT,unary(&p,6,dx),unary(&p,6,dy)),unary(&p,6,dx),unary(&p,6,dy));
      value = bin(&p,DXIL_BINOP_ADD,value,bin(&p,DXIL_BINOP_ADD,bin(&p,DXIL_BINOP_MUL,slope,pick(&p,front,fw(&p,o->front_scale_word),fw(&p,o->back_scale_word))),pick(&p,front,fw(&p,o->front_offset_word),fw(&p,o->back_offset_word))));
    }
    if (float24) {
      if (!o->writes_depth) value = xe_dxil_alu_saturate(&p.alu,bin(&p,DXIL_BINOP_MUL,value,fc(&p,2)));
      value = xe_dxil_unpack_depth20e4(&p.alu,xe_dxil_pack_depth20e4(&p.alu,value,!truncate,0),1);
    } else if (o->writes_depth) value = pick(&p,flag(&p,flags,1u<<10),bin(&p,DXIL_BINOP_MUL,value,fc(&p,0.5f)),value);
    if (!output(&p,(unsigned)depth_id,0,0,value)) goto done;
  }
  if (o->count_coverage) {
    args[0] = ic(&p,91); coverage = bin(&p,DXIL_BINOP_AND,call(&p,"dx.op.coverage",DXIL_I32,args,1),mask);
    if (depth_id >= 0 && !o->writes_depth) {
      args[0] = ic(&p,90); coverage = bin(&p,DXIL_BINOP_AND,coverage,bin(&p,DXIL_BINOP_SHL,ic(&p,1),call(&p,"dx.op.sampleIndex",DXIL_I32,args,1)));
    }
    args[0] = ic(&p,31); args[1] = coverage; value = call(&p,"dx.op.unaryBits",DXIL_I32,args,2);
    index = word(&p,o->counter_word);
    /* The sentinel must not address the UAV, even for an atomic add of zero. */
    if (!dxil_function_reserve_blocks(p.alu.module,2,&n)) goto done;
    if (!dxil_emit_branch(p.alu.module,cmp(&p,DXIL_ICMP_NE,index,ic(&p,UINT32_MAX)),n,n+1)) goto done;
    p.alu.module->cur_emitting_func->basic_block_ids[n] = (int)p.alu.module->cur_emitting_func->curr_block;
    args[0] = ic(&p,78); args[1] = counters; args[2] = ic(&p,0); args[3] = bin(&p,DXIL_BINOP_MUL,index,ic(&p,o->counter_stride_bytes)); args[4] = ic(&p,0); args[5] = args[4]; args[6] = value;
    if (!call(&p,"dx.op.atomicBinOp",DXIL_I32,args,7) || !dxil_emit_branch(p.alu.module,NULL,n+1,0)) goto done;
    p.alu.module->cur_emitting_func->basic_block_ids[n+1] = (int)p.alu.module->cur_emitting_func->curr_block;
  }
complete:
  if (p.alu.failed || xe_dxil_guest_program_alu(program)->failed || !dxil_emit_ret_void(p.alu.module)) goto done;
  binary = (XeDxilShaderBinary *)calloc(1,sizeof(*binary));
  if (binary && !xe_dxil_shader_finish(p.shader,binary)) { xe_dxil_shader_binary_delete(binary); binary = NULL; }
done:
  xe_dxil_guest_rop_destroy(p.rop); xe_dxil_guest_program_destroy(program); xe_dxil_shader_destroy(p.shader); return binary;
}
