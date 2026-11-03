/* Native guest vertex stage. MSVC C89; BSD license.
 * Guest index, viewport, clip and kill rules follow Xenia's vertex epilogue. */
#include "stage_layout.h"
#include "guest_vertex_stage.h"
#include <dxil_module.h>
#include <dxil_function.h>
typedef struct Vertex {
  XeDxilShaderModule *shader;
  XeDxilAlu alu;
  XeDxilValue system;
  XeDxilTextureStage *textures;
  XeDxilGuestProgramOptions user;
} Vertex;
static int texture_fetch(void *context, XeDxilAlu *alu, XeDxilAluIo *io,
    const XeDxilTextureInstruction *instruction, const XeDxilTextureOperands *operands,
    unsigned serialized, XeDxilValue result[4]) {
  Vertex *v = (Vertex *)context; (void)serialized;
  return xe_dxil_texture_stage_fetch(v->textures,alu,io,instruction,operands,result);
}
static int observed_export(void *context, XeDxilGuestProgram *program, unsigned index, unsigned mask) {
  Vertex *v = (Vertex *)context;
  return !v->user.export_written || v->user.export_written(v->user.context,program,index,mask);
}
static XeDxilValue ic(Vertex *v, uint32_t i) { return dxil_module_get_int32_const(v->alu.module,(int32_t)i); }
static XeDxilValue fc(Vertex *v, float f) { return dxil_module_get_float_const(v->alu.module,f); }
static XeDxilValue bin(Vertex *v, enum dxil_bin_opcode op, XeDxilValue a, XeDxilValue b) {
  XeDxilValue value = a && b ? dxil_emit_binop(v->alu.module,op,a,b,0) : NULL;
  if (!value) v->alu.failed = 1;
  return value;
}
static XeDxilValue cmp(Vertex *v, enum dxil_cmp_pred op, XeDxilValue a, XeDxilValue b) {
  XeDxilValue value = a && b ? dxil_emit_cmp(v->alu.module,op,a,b) : NULL;
  if (!value) v->alu.failed = 1;
  return value;
}
static XeDxilValue pick(Vertex *v, XeDxilValue c, XeDxilValue a, XeDxilValue b) {
  XeDxilValue value = c && a && b ? dxil_emit_select(v->alu.module,c,a,b) : NULL;
  if (!value) v->alu.failed = 1;
  return value;
}
static XeDxilValue cast(Vertex *v, enum dxil_cast_opcode op, int floating, XeDxilValue value) {
  value = value ? dxil_emit_cast(v->alu.module,op,floating ? dxil_module_get_float_type(v->alu.module,32) :
    dxil_module_get_int_type(v->alu.module,32),value) : NULL;
  if (!value) v->alu.failed = 1;
  return value;
}
static XeDxilValue word(Vertex *v, unsigned i) { return xe_dxil_cbuffer_word(&v->alu,v->system,ic(v,i)); }
static XeDxilValue field_float(Vertex *v, unsigned i) { return cast(v,DXIL_CAST_BITCAST,1,word(v,i)); }
static XeDxilValue flag(Vertex *v, XeDxilValue flags, unsigned mask) {
  return cmp(v,DXIL_ICMP_NE,bin(v,DXIL_BINOP_AND,flags,ic(v,mask)),ic(v,0));
}
static int output(Vertex *v, unsigned id, unsigned component, XeDxilValue value) {
  XeDxilValue args[5];
  const struct dxil_func *function = dxil_get_function(v->alu.module,"dx.op.storeOutput",DXIL_F32);
  args[0] = ic(v,5); args[1] = ic(v,id); args[2] = ic(v,0);
  args[3] = dxil_module_get_int8_const(v->alu.module,(int8_t)component); args[4] = value;
  return function && value && dxil_emit_call_void(v->alu.module,function,args,5);
}
static XeDxilValue swap_index(Vertex *v, XeDxilValue index, XeDxilValue endian) {
  XeDxilValue swapped, condition;
  swapped = bin(v,DXIL_BINOP_OR,
    bin(v,DXIL_BINOP_SHL,bin(v,DXIL_BINOP_AND,index,ic(v,0x00ff00ff)),ic(v,8)),
    bin(v,DXIL_BINOP_LSHR,bin(v,DXIL_BINOP_AND,index,ic(v,0xff00ff00)),ic(v,8)));
  condition = bin(v,DXIL_BINOP_OR,cmp(v,DXIL_ICMP_EQ,endian,ic(v,1)),cmp(v,DXIL_ICMP_EQ,endian,ic(v,2)));
  index = pick(v,condition,swapped,index);
  swapped = bin(v,DXIL_BINOP_OR,bin(v,DXIL_BINOP_SHL,index,ic(v,16)),bin(v,DXIL_BINOP_LSHR,index,ic(v,16)));
  condition = bin(v,DXIL_BINOP_OR,cmp(v,DXIL_ICMP_EQ,endian,ic(v,2)),cmp(v,DXIL_ICMP_EQ,endian,ic(v,3)));
  return pick(v,condition,swapped,index);
}
static int distance_signature(Vertex *v, unsigned kind, unsigned system, const char *name,
    unsigned count, unsigned offset, int ids[2]) {
  unsigned i, n;
  for (i = 0; i < count; i += 4) {
    n = count-i; if (n > 4) n = 4;
    ids[i/4] = xe_dxil_shader_signature(v->shader,1,name,i/4,kind,system,
      XE_DXIL_GUEST_DISTANCE_ROW+(offset+i)/4,(offset+i)%4,n,0);
    if (ids[i/4] < 0) return 0;
  }
  return 1;
}
static int domain_registers(Vertex *v, XeDxilGuestProgram *program, unsigned domain, unsigned points) {
  const struct dxil_func *location = dxil_get_function(v->alu.module,"dx.op.domainLocation",DXIL_F32);
  const struct dxil_func *load = dxil_get_function(v->alu.module,"dx.op.loadInput",DXIL_F32);
  XeDxilValue args[5], coordinates[3], indices[4], r0[4], r1[4]; unsigned i;
  for (i = 0; i < (domain == 2 ? 3u : 2u); ++i) {
    args[0] = ic(v,105); args[1] = dxil_module_get_int8_const(v->alu.module,(int8_t)i);
    coordinates[i] = location ? dxil_emit_call(v->alu.module,location,args,2) : NULL;
    if (!coordinates[i]) return 0;
  }
  for (i = 0; i < points; ++i) {
    args[0] = ic(v,4); args[1] = args[2] = ic(v,0);
    args[3] = dxil_module_get_int8_const(v->alu.module,0); args[4] = ic(v,i);
    indices[i] = load ? dxil_emit_call(v->alu.module,load,args,5) : NULL;
    if (!indices[i]) return 0;
  }
  for (i = 0; i < 4; ++i) r0[i] = r1[i] = fc(v,0);
  if (domain == 2) {
    r0[0] = coordinates[2]; r0[1] = coordinates[1]; r0[2] = coordinates[0]; r0[3] = fc(v,1);
    for (i = 0; i < points; ++i) r1[i] = indices[i];
  } else if (points == 1) {
    r0[0] = indices[0]; r0[1] = coordinates[0]; r0[2] = coordinates[1]; r0[3] = fc(v,1);
  } else {
    r0[0] = coordinates[0]; r0[1] = coordinates[1]; r0[2] = indices[0];
    for (i = 1; i < 4; ++i) r1[i-1] = indices[i];
  }
  return xe_dxil_guest_program_register(program,0,15,r0) && xe_dxil_guest_program_register(program,1,15,r1);
}
int xe_dxil_build_guest_vertex(const XeDxilGuestVertexOptions *o, const uint32_t *words,
    size_t word_count, unsigned cf_count, XeDxilShaderBinary *binary) {
  Vertex v;
  XeDxilGuestProgramOptions options;
  XeDxilGuestProgram *program = NULL;
  XeDxilValue args[5], index, values[4], position[4], flags, point_size, killed, distance, value;
  const struct dxil_func *load;
  unsigned i, j, row, cbv = 1, culls, interpolator_count = 0, packed;
  int position_id, interpolators[16], clip_ids[2], cull_ids[2], point_id = -1, resource, result = 0;
  if (!o || !binary || o->program.register_count > 256 || o->program.float_constant_count > 256 ||
      o->interpolator_mask > 65535 || o->clip_planes > 6 || o->cull_planes > 6 || (o->clip_planes && o->cull_planes) ||
      o->system_constants_bytes < 4 || o->system_constants_bytes > 65536) return 0;
  if (o->domain && ((o->domain != 2 && o->domain != 3) || o->program.register_count < 2 ||
      (o->control_points != 1 && o->control_points != (o->domain == 2 ? 3u : 4u)))) return 0;
  i = o->system_constants_bytes/4;
  if (o->textures && (o->textures->pixel_shader || o->textures->system_constants_bytes != o->system_constants_bytes ||
      o->program.texture || o->program.allocation || o->program.kill)) return 0;
  if (o->flags_word >= i || o->index_endian_word >= i || o->index_base_word >= i ||
      i < 3 || o->ndc_scale_word > i-3 || o->ndc_offset_word > i-3 ||
      ((o->clip_planes || o->cull_planes) && (o->clip_planes_word > i || (o->clip_planes+o->cull_planes)*4 > i-o->clip_planes_word))) return 0;
  for (i = 0; i < 16; ++i) if (o->interpolator_mask & (1u<<i)) ++interpolator_count;
  memset(&v,0,sizeof(v)); options = o->program;
  v.shader = xe_dxil_shader_create(o->domain ? DXIL_DOMAIN_SHADER : DXIL_VERTEX_SHADER); if (!v.shader) return 0;
  if (o->textures && !xe_dxil_shader_bindless(v.shader)) goto done;
  xe_dxil_alu_init(&v.alu,xe_dxil_shader_module(v.shader));
  if (o->domain) {
    if (!xe_dxil_shader_domain(v.shader,o->domain,o->control_points) ||
        xe_dxil_shader_signature(v.shader,0,"XEVERTEXID",0,DXIL_SEM_ARBITRARY,0,0,0,1,0) != 0) goto done;
  } else if (xe_dxil_shader_signature_typed(v.shader,0,"SV_VertexID",0,DXIL_SEM_VERTEX_ID,DXIL_PROG_SEM_VERTEX_ID,
        0,0,1,0,DXIL_COMP_TYPE_U32) != 0) goto done;
  position_id = xe_dxil_shader_signature(v.shader,1,"SV_Position",0,DXIL_SEM_POSITION,DXIL_PROG_SEM_POSITION,XE_DXIL_GUEST_POSITION_ROW,0,4,0);
  culls = o->cull_planes+(o->vertex_kill && o->vertex_kill_and ? 1 : 0);
  if (position_id < 0 || !distance_signature(&v,DXIL_SEM_CLIP_DISTANCE,DXIL_PROG_SEM_CLIP_DISTANCE,"SV_ClipDistance",o->clip_planes,0,clip_ids) ||
      !distance_signature(&v,DXIL_SEM_CULL_DISTANCE,DXIL_PROG_SEM_CULL_DISTANCE,"SV_CullDistance",culls,o->clip_planes,cull_ids)) goto done;
  row = 0;
  for (i = 0; i < interpolator_count; ++i) {
    interpolators[i] = xe_dxil_shader_signature(v.shader,1,"TEXCOORD",i,DXIL_SEM_ARBITRARY,0,row++,0,4,0);
    if (interpolators[i] < 0) goto done;
  }
  if (o->point_size) {
    point_id = xe_dxil_shader_signature(v.shader,1,"TEXCOORD",interpolator_count,DXIL_SEM_ARBITRARY,0,row++,0,1,0);
    if (point_id < 0) goto done;
  }
  if (xe_dxil_shader_resource(v.shader,XE_DXIL_RESOURCE_CBV,DXIL_RESOURCE_KIND_CBUFFER,1,0,1,o->system_constants_bytes) != 0) goto done;
  v.system = xe_dxil_resource_handle(&v.alu,XE_DXIL_RESOURCE_CBV,0,0);
  options.float_constants = NULL;
  if (options.float_constant_count) {
    if (xe_dxil_shader_resource(v.shader,XE_DXIL_RESOURCE_CBV,DXIL_RESOURCE_KIND_CBUFFER,1,1,1,options.float_constant_count*16) != (int)cbv) goto done;
    options.float_constants = xe_dxil_resource_handle(&v.alu,XE_DXIL_RESOURCE_CBV,cbv++,1);
  }
  resource = xe_dxil_shader_resource(v.shader,XE_DXIL_RESOURCE_CBV,DXIL_RESOURCE_KIND_CBUFFER,1,3,1,160);
  if (resource != (int)cbv++) goto done;
  options.bool_loop_constants = xe_dxil_resource_handle(&v.alu,XE_DXIL_RESOURCE_CBV,(unsigned)resource,3);
  resource = xe_dxil_shader_resource(v.shader,XE_DXIL_RESOURCE_CBV,DXIL_RESOURCE_KIND_CBUFFER,1,4,1,768);
  if (resource != (int)cbv || xe_dxil_shader_resource(v.shader,XE_DXIL_RESOURCE_SRV,DXIL_RESOURCE_KIND_RAW_BUFFER,0,0,1,0) != 0) goto done;
  options.fetch_constants = xe_dxil_resource_handle(&v.alu,XE_DXIL_RESOURCE_CBV,cbv,4);
  options.shared_memory = xe_dxil_resource_handle(&v.alu,XE_DXIL_RESOURCE_SRV,0,0);
  if (options.memexport_mask) {
    resource = xe_dxil_shader_resource(v.shader,XE_DXIL_RESOURCE_UAV,DXIL_RESOURCE_KIND_RAW_BUFFER,0,0,1,0);
    if (resource < 0) goto done;
    options.memexport_uav = xe_dxil_resource_handle(&v.alu,XE_DXIL_RESOURCE_UAV,(unsigned)resource,0);
    /* Shared-memory reads during a write draw use its UAV resource state. */
    options.shared_memory = options.memexport_uav;
  }
  if (o->textures) {
    v.textures = xe_dxil_texture_stage_create(v.shader,v.system,options.fetch_constants,o->textures);
    if (!v.textures) goto done;
    v.user = o->program; options.context = &v; options.texture = texture_fetch; options.export_written = observed_export;
  }
  program = xe_dxil_guest_program_create(v.alu.module,&options); if (!program) goto done;
  if (o->domain) {
    if (!domain_registers(&v,program,o->domain,o->control_points)) goto done;
  } else {
  load = dxil_get_function(v.alu.module,"dx.op.loadInput",DXIL_I32);
  args[0] = ic(&v,4); args[1] = ic(&v,0); args[2] = args[1]; args[3] = dxil_module_get_int8_const(v.alu.module,0);
  args[4] = dxil_module_get_undef(v.alu.module,dxil_module_get_int_type(v.alu.module,32));
  index = load ? dxil_emit_call(v.alu.module,load,args,5) : NULL;
  index = swap_index(&v,index,word(&v,o->index_endian_word));
  index = bin(&v,DXIL_BINOP_ADD,index,word(&v,o->index_base_word));
  values[0] = cast(&v,DXIL_CAST_SITOFP,1,index);
  for (i = 1; i < 4; ++i) values[i] = fc(&v,0);
  if (options.register_count && !xe_dxil_guest_program_register(program,0,15,values)) goto done;
  }
  if (o->point_size) {
    values[0] = fc(&v,-1); if (!xe_dxil_guest_program_default_export(program,63,1,values)) goto done;
  }
  if (!xe_dxil_guest_program_emit(program,words,word_count,cf_count)) goto done;
  for (i = 0; i < 4; ++i) position[i] = xe_dxil_guest_program_export(program,62,i);
  flags = word(&v,o->flags_word);
  position[3] = pick(&v,flag(&v,flags,o->w_not_reciprocal_mask),position[3],bin(&v,DXIL_BINOP_SDIV,fc(&v,1),position[3]));
  for (i = 0; i < 3; ++i) position[i] = pick(&v,flag(&v,flags,i < 2 ? o->xy_divided_mask : o->z_divided_mask),
    bin(&v,DXIL_BINOP_MUL,position[i],position[3]),position[i]);
  for (i = 0; i < o->clip_planes+o->cull_planes; ++i) {
    distance = fc(&v,0);
    for (j = 0; j < 4; ++j) distance = bin(&v,DXIL_BINOP_ADD,distance,
      bin(&v,DXIL_BINOP_MUL,position[j],field_float(&v,o->clip_planes_word+i*4+j)));
    if (i < o->clip_planes) {
      if (!output(&v,(unsigned)clip_ids[i/4],i%4,distance)) goto done;
    } else if (!output(&v,(unsigned)cull_ids[(i-o->clip_planes)/4],(i-o->clip_planes)%4,distance)) goto done;
  }
  for (i = 0; i < 3; ++i) position[i] = bin(&v,DXIL_BINOP_ADD,
    bin(&v,DXIL_BINOP_MUL,position[i],field_float(&v,o->ndc_scale_word+i)),
    bin(&v,DXIL_BINOP_MUL,field_float(&v,o->ndc_offset_word+i),position[3]));
  if (o->vertex_kill) {
    value = cast(&v,DXIL_CAST_BITCAST,0,xe_dxil_guest_program_export(program,63,2));
    killed = cmp(&v,DXIL_ICMP_NE,bin(&v,DXIL_BINOP_AND,value,ic(&v,0x7fffffff)),ic(&v,0));
    if (o->vertex_kill_and) {
      if (!output(&v,(unsigned)cull_ids[o->cull_planes/4],o->cull_planes%4,pick(&v,killed,fc(&v,-1),fc(&v,0)))) goto done;
    } else position[3] = pick(&v,killed,cast(&v,DXIL_CAST_BITCAST,1,ic(&v,0x7fc00000)),position[3]);
  }
  for (i = 0; i < 4; ++i) if (!output(&v,(unsigned)position_id,i,position[i])) goto done;
  packed = 0;
  for (i = 0; i < 16; ++i) if (o->interpolator_mask & (1u<<i)) {
    for (j = 0; j < 4; ++j)
      if (!output(&v,(unsigned)interpolators[packed],j,xe_dxil_guest_program_export(program,i,j))) goto done;
    ++packed;
  }
  if (point_id >= 0) {
    point_size = xe_dxil_guest_program_export(program,63,0);
    if (!output(&v,(unsigned)point_id,0,point_size)) goto done;
  }
  if (!v.alu.failed && !xe_dxil_guest_program_alu(program)->failed && dxil_emit_ret_void(v.alu.module)) result = xe_dxil_shader_finish(v.shader,binary);
done:
  xe_dxil_texture_stage_destroy(v.textures); xe_dxil_guest_program_destroy(program); xe_dxil_shader_destroy(v.shader); return result;
}
XeDxilShaderBinary *xe_dxil_build_guest_vertex_owned(const XeDxilGuestVertexOptions *options,
    const uint32_t *words, size_t word_count, unsigned cf_count) {
  XeDxilShaderBinary *binary = (XeDxilShaderBinary *)calloc(1,sizeof(*binary));
  if (!binary) return NULL;
  if (!xe_dxil_build_guest_vertex(options,words,word_count,cf_count,binary)) {
    xe_dxil_shader_binary_delete(binary); return NULL;
  }
  return binary;
}
