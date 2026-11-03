/* Native stage metadata and serialization. MSVC C89; BSD license. */
#include "shader_module.h"
#include <dxil_module.h>
#include <dxil_function.h>
#include <dxil_container.h>

struct XeDxilShaderModule {
  void *arena;
  struct dxil_module module;
  struct dxil_validation_state validation;
  struct dxil_resource_v1 resource[128];
  const struct dxil_mdnode *resource_nodes[4][32];
  unsigned resource_counts[4];
  unsigned stage_values[5];
  unsigned stage_tag, stage_count, raw_buffers;
  uint64_t shader_flags;
  unsigned sample_shadow;
  int finished, failed;
};

static const struct dxil_mdnode *metadata_int(XeDxilShaderModule *s, unsigned value) {
  const struct dxil_mdnode *node = dxil_get_metadata_int32(&s->module, (int32_t)value);
  if (!node) s->failed = 1;
  return node;
}

XeDxilShaderModule *xe_dxil_shader_create(unsigned stage) {
  XeDxilShaderModule *s;
  const struct dxil_type *type;
  struct dxil_func_def *function;
  static const char *attributes[] = {NULL};
  void *arena;
  if (stage > 5) return NULL;
  s = (XeDxilShaderModule *)calloc(1, sizeof(*s));
  arena = ralloc_size(NULL, 0);
  if (!s || !arena) { free(s); ralloc_free(arena); return NULL; }
  s->arena = arena;
  dxil_module_init(&s->module, arena);
  s->module.native_shader_owner = s;
  s->module.shader_kind = (enum dxil_shader_kind)stage;
  s->module.major_version = 6; s->module.minor_version = 0;
  s->module.major_validator = 1; s->module.minor_validator = 6;
  s->module.sem_string_table = _mesa_string_buffer_create(arena, 16);
  if (!s->module.sem_string_table || !_mesa_string_buffer_append_len(s->module.sem_string_table, "", 1)) goto fail;
  type = dxil_module_add_function_type(&s->module, dxil_module_get_void_type(&s->module), NULL, 0);
  function = type ? dxil_add_function_def(&s->module, "main", type, 1, attributes, attributes) : NULL;
  if (!function) goto fail;
  function->basic_block_ids[0] = 0;
  s->validation.state.psv2.psv1.shader_stage = (uint8_t)stage;
  s->validation.state.psv2.psv1.psv0.max_expected_wave_lane_count = UINT32_MAX;
  s->validation.resources.v1 = s->resource;
  if (stage == DXIL_COMPUTE_SHADER && !xe_dxil_shader_threads(s, 1, 1, 1)) goto fail;
  return s;
fail:
  xe_dxil_shader_destroy(s);
  return NULL;
}

struct dxil_module *xe_dxil_shader_module(XeDxilShaderModule *s) {
  return s && !s->finished && !s->failed ? &s->module : NULL;
}

void xe_dxil_shader_destroy(XeDxilShaderModule *s) {
  if (!s) return;
  dxil_module_release(&s->module);
  ralloc_free(s->arena); free(s);
}

int xe_dxil_shader_signature(XeDxilShaderModule *s, unsigned direction,
                               const char *name, unsigned index, unsigned kind, unsigned system,
                               unsigned row, unsigned column, unsigned columns, unsigned interpolation) {
  return xe_dxil_shader_signature_typed(s, direction, name, index, kind, system,
    row, column, columns, interpolation, DXIL_COMP_TYPE_F32);
}

int xe_dxil_shader_signature_typed(XeDxilShaderModule *s, unsigned direction,
                                     const char *name, unsigned index, unsigned kind, unsigned system,
                                     unsigned row, unsigned column, unsigned columns, unsigned interpolation,
                                     unsigned component_type) {
  struct dxil_signature_record *record;
  struct dxil_psv_signature_element *psv;
  struct dxil_module *m;
  unsigned id;
  size_t name_length;
  if (!s || s->failed || s->finished) return -1;
  m = &s->module;
  if (direction > 2 || !name || !columns || column > 3 || columns > 4 - column ||
      (row >= 32 && row != UINT32_MAX) || kind > 255 || interpolation > 7 || m->sem_index_table.size >= 128 ||
      (component_type != DXIL_COMP_TYPE_F32 && component_type != DXIL_COMP_TYPE_U32 && component_type != DXIL_COMP_TYPE_I32)) goto fail;
  name_length = strlen(name);
  if (name_length >= UINT32_MAX) goto fail;
  if (!direction) {
    id = m->num_sig_inputs;
    if (id >= 32) goto fail;
    record = &m->inputs[id]; psv = &m->psv_inputs[id]; ++m->num_sig_inputs;
    if (row != UINT32_MAX && m->num_psv_inputs < row + 1) m->num_psv_inputs = row + 1;
  } else if (direction == 1) {
    id = m->num_sig_outputs;
    if (id >= 32) goto fail;
    record = &m->outputs[id]; psv = &m->psv_outputs[id]; ++m->num_sig_outputs;
    if (row != UINT32_MAX && m->num_psv_outputs[0] < row + 1) m->num_psv_outputs[0] = row + 1;
  } else {
    id = m->num_sig_patch_consts;
    if (id >= 32) goto fail;
    record = &m->patch_consts[id]; psv = &m->psv_patch_consts[id]; ++m->num_sig_patch_consts;
    if (row != UINT32_MAX && m->num_psv_patch_consts < row + 1) m->num_psv_patch_consts = row + 1;
  }
  record->num_elements = 1; record->name = ralloc_strdup(m->ralloc_ctx, name);
  if (!record->name) goto fail;
  record->sig_comp_type = (uint8_t)component_type;
  record->elements[0].semantic_index = index; record->elements[0].system_value = system;
  record->elements[0].comp_type = component_type == DXIL_COMP_TYPE_F32 ? DXIL_PROG_SIG_COMP_TYPE_FLOAT32 :
    component_type == DXIL_COMP_TYPE_U32 ? DXIL_PROG_SIG_COMP_TYPE_UINT32 : DXIL_PROG_SIG_COMP_TYPE_SINT32;
  record->elements[0].reg = row; record->elements[0].mask = (uint8_t)(((1u << columns) - 1) << column);
  if (!direction) record->elements[0].always_reads_mask = record->elements[0].mask;
  else record->elements[0].never_writes_mask = (uint8_t)(15u & ~record->elements[0].mask);
  psv->rows = 1; psv->start_row = row == UINT32_MAX ? 0 : (uint8_t)row;
  psv->cols_and_start = (uint8_t)((row == UINT32_MAX ? 0 : 64) + column * 16 + columns);
  psv->semantic_kind = (uint8_t)kind; psv->component_type = (uint8_t)record->elements[0].comp_type;
  psv->interpolation_mode = (uint8_t)interpolation;
  psv->semantic_indexes_offset = m->sem_index_table.size;
  if (kind == DXIL_SEM_ARBITRARY) {
    psv->semantic_name_offset = m->sem_string_table->length;
    if (!_mesa_string_buffer_append_len(m->sem_string_table, name, (uint32_t)name_length + 1)) goto fail;
  }
  m->sem_index_table.data[m->sem_index_table.size++] = index;
  if (direction == 1 && kind == DXIL_SEM_POSITION) {
    if (m->shader_kind == DXIL_VERTEX_SHADER) s->validation.state.psv2.psv1.psv0.vs.output_position_present = 1;
    if (m->shader_kind == DXIL_GEOMETRY_SHADER) s->validation.state.psv2.psv1.psv0.gs.output_position_present = 1;
    if (m->shader_kind == DXIL_DOMAIN_SHADER) s->validation.state.psv2.psv1.psv0.ds.output_position_present = 1;
  }
  if (direction == 1 && (kind == DXIL_SEM_DEPTH || kind == DXIL_SEM_DEPTH_LE || kind == DXIL_SEM_DEPTH_GE))
    s->validation.state.psv2.psv1.psv0.ps.depth_output = 1;
  if (direction == 1 && kind == DXIL_SEM_STENCIL_REF) { s->module.feats.stencil_ref = 1; s->shader_flags |= 2048; }
  return (int)id;
fail:
  s->failed = 1;
  return -1;
}

int xe_dxil_shader_barycentrics(XeDxilShaderModule *s) {
  if (!s || s->failed || s->finished || s->module.shader_kind != DXIL_PIXEL_SHADER) return -1;
  if (s->module.minor_version < 1) s->module.minor_version = 1;
  s->module.feats.barycentrics = 1; s->shader_flags |= 1u<<22;
  return xe_dxil_shader_signature(s,0,"SV_Barycentrics",0,DXIL_SEM_BARYCENTRICS,
    DXIL_PROG_SEM_BARYCENTRICS,UINT32_MAX,0,3,DXIL_INTERP_LINEAR);
}
int xe_dxil_shader_sample_frequency(XeDxilShaderModule *s) {
  if (!s || s->failed || s->finished || s->module.shader_kind != DXIL_PIXEL_SHADER) return 0;
  if (s->sample_shadow) return 1;
  if (xe_dxil_shader_signature_typed(s, 0, "SV_SampleIndex", 0, DXIL_SEM_SAMPLE_INDEX,
      DXIL_PROG_SEM_SAMPLE_INDEX, UINT32_MAX, 0, 1, DXIL_INTERP_CONSTANT, DXIL_COMP_TYPE_U32) < 0) return 0;
  s->validation.state.psv2.psv1.psv0.ps.sample_frequency = 1;
  s->sample_shadow = 1;
  return 1;
}

int xe_dxil_shader_resource(XeDxilShaderModule *s, unsigned resource_class, unsigned kind,
                              unsigned space, unsigned binding, unsigned count, unsigned size) {
  return xe_dxil_shader_resource_typed(s, resource_class, kind, space, binding, count, size, DXIL_COMP_TYPE_F32);
}

static int resource_impl(XeDxilShaderModule *s, unsigned resource_class, unsigned kind,
                                    unsigned space, unsigned binding, unsigned count, unsigned size,
                                    unsigned component_type, unsigned ordered) {
  const struct dxil_mdnode *fields[11], *pair[2], *node;
  const struct dxil_type *element, *type;
  struct dxil_resource_v1 *resource;
  struct dxil_module *m;
  unsigned id, field_count, i;
  char type_name[64];
  if (!s || s->failed || s->finished) return -1;
  if (ordered && (s->module.shader_kind != DXIL_PIXEL_SHADER || resource_class != 1)) goto fail;
  if (resource_class > 3 || !count || (count != UINT32_MAX && count - 1 > UINT32_MAX - binding) ||
      !kind || kind > 14 || s->resource_counts[resource_class] >= 32 ||
      (component_type != DXIL_COMP_TYPE_F32 && component_type != DXIL_COMP_TYPE_U32 && component_type != DXIL_COMP_TYPE_I32)) goto fail;
  m = &s->module; id = s->resource_counts[resource_class];
  element = dxil_module_get_int_type(m, 32);
  if (resource_class == DXIL_RESOURCE_CLASS_CBV) {
    if (!size || size > 65536) goto fail;
    element = dxil_module_get_array_type(m, element, (size + 3) / 4);
  } else if (resource_class < 2 && kind != DXIL_RESOURCE_KIND_RAW_BUFFER) {
    element = dxil_module_get_vector_type(m, component_type == DXIL_COMP_TYPE_F32 ?
      dxil_module_get_float_type(m, 32) : dxil_module_get_int_type(m, 32), 4);
  }
  /* Class <= 3 and id < 32 above: at most 14 bytes including NUL. */
  sprintf(type_name, "Resource_%u_%u", resource_class, id);
  type = dxil_module_get_struct_type(m, type_name, &element, 1);
  if (count != 1) type = dxil_module_get_array_type(m, type, count);
  type = type ? dxil_module_get_pointer_type(m, type) : NULL;
  if (!type) goto fail;
  for (i = 0; i < 11; ++i) fields[i] = NULL;
  fields[0] = metadata_int(s, id);
  fields[1] = dxil_get_metadata_value(m, type, dxil_module_get_undef(m, type));
  fields[2] = dxil_get_metadata_string(m, "");
  fields[3] = metadata_int(s, space); fields[4] = metadata_int(s, binding); fields[5] = metadata_int(s, count);
  if (resource_class == 2) { fields[6] = metadata_int(s, size); field_count = 8; }
  else if (resource_class == 3) { fields[6] = metadata_int(s, 0); field_count = 8; }
  else {
    fields[6] = metadata_int(s, kind);
    fields[7] = resource_class ? dxil_get_metadata_int1(m, 0) : metadata_int(s, 0);
    if (resource_class) { fields[8] = fields[7]; fields[9] = dxil_get_metadata_int1(m, ordered != 0); field_count = 11; }
    else field_count = 9;
    if (kind != DXIL_RESOURCE_KIND_RAW_BUFFER) {
      pair[0] = metadata_int(s, 0); pair[1] = metadata_int(s, component_type);
      fields[field_count - 1] = dxil_get_metadata_node(m, pair, 2);
    }
  }
  if (!fields[1] || !fields[2] || s->failed) goto fail;
  node = dxil_get_metadata_node(m, fields, field_count);
  if (!node) goto fail;
  s->resource_nodes[resource_class][id] = node; ++s->resource_counts[resource_class];
  /* PSV binding records are ordered CBV, sampler, SRV, UAV regardless of
   * declaration order. Insert directly into that order, preserving each
   * class's resource IDs; metadata construction uses separate class lists. */
  i = resource_class == 2 ? id : s->resource_counts[2];
  if (resource_class != 2 && resource_class != 3) i += s->resource_counts[3];
  if (resource_class == 3) i += id;
  else if (resource_class == 0) i += id;
  else if (resource_class == 1) i += s->resource_counts[0] + id;
  memmove(&s->resource[i + 1], &s->resource[i],
    (s->validation.num_resources - i) * sizeof(s->resource[0]));
  resource = &s->resource[i]; memset(resource, 0, sizeof(*resource));
  ++s->validation.num_resources;
  resource->v0.space = space; resource->v0.lower_bound = binding;
  resource->v0.upper_bound = count == UINT32_MAX ? UINT32_MAX : binding + count - 1;
  resource->resource_kind = kind;
  if (kind == DXIL_RESOURCE_KIND_RAW_BUFFER) s->raw_buffers = 1;
  if (ordered) { m->feats.rovs = 1; s->shader_flags |= 262144; }
  if (resource_class == 1 && m->shader_kind != DXIL_PIXEL_SHADER && m->shader_kind != DXIL_COMPUTE_SHADER) {
    m->feats.uavs_at_every_stage = 1; s->shader_flags |= 65536;
  }
  resource->v0.resource_type = resource_class == 2 ? DXIL_RES_CBV : resource_class == 3 ? DXIL_RES_SAMPLER :
    resource_class ? kind == DXIL_RESOURCE_KIND_RAW_BUFFER ? DXIL_RES_UAV_RAW : DXIL_RES_UAV_TYPED :
    kind == DXIL_RESOURCE_KIND_RAW_BUFFER ? DXIL_RES_SRV_RAW : DXIL_RES_SRV_TYPED;
  return (int)id;
fail:
  s->failed = 1;
  return -1;
}

int xe_dxil_shader_resource_typed(XeDxilShaderModule *s, unsigned resource_class, unsigned kind,
    unsigned space, unsigned binding, unsigned count, unsigned size, unsigned component_type) {
  return resource_impl(s, resource_class, kind, space, binding, count, size, component_type, 0);
}
int xe_dxil_shader_ordered_buffer(XeDxilShaderModule *s, unsigned space, unsigned binding) {
  return resource_impl(s, 1, DXIL_RESOURCE_KIND_RAW_BUFFER, space, binding, 1, 0, DXIL_COMP_TYPE_U32, 1);
}
int xe_dxil_shader_bindless(XeDxilShaderModule *s) {
  if (!s || s->failed || s->finished) return 0;
  s->module.minor_version = 6;
  return 1;
}
static const struct dxil_value *annotate(XeDxilShaderModule *s, unsigned resource_class,
    unsigned id, const struct dxil_value *handle) {
  const struct dxil_func *function;
  const struct dxil_value *args[3];
  if (!handle) return NULL;
  args[0] = dxil_module_get_int32_const(&s->module,216); args[1] = handle;
  args[2] = dxil_module_get_res_props_const(&s->module,(enum dxil_resource_class)resource_class,
    s->resource_nodes[resource_class][id]);
  function = dxil_get_function(&s->module,"dx.op.annotateHandle",DXIL_NONE);
  return args[0] && args[2] && function ? dxil_emit_call(&s->module,function,args,3) : NULL;
}
const struct dxil_value *xe_dxil_shader_bound_handle(XeDxilShaderModule *s,
    unsigned resource_class, unsigned id, unsigned binding) {
  struct dxil_module *m;
  const struct dxil_resource_v0 *resource;
  const struct dxil_func *function;
  const struct dxil_value *args[4], *value;
  unsigned offset;
  if (!s || s->failed || s->finished || s->module.minor_version < 6 || resource_class > 3 ||
      id >= s->resource_counts[resource_class]) return NULL;
  m = &s->module;
  offset = resource_class == 2 ? 0 : s->resource_counts[2];
  if (resource_class < 2) offset += s->resource_counts[3];
  if (resource_class == 1) offset += s->resource_counts[0];
  resource = &s->resource[offset+id].v0;
  if (binding < resource->lower_bound || binding > resource->upper_bound) return NULL;
  args[0] = dxil_module_get_int32_const(m,217);
  args[1] = dxil_module_get_res_bind_const(m,resource->lower_bound,resource->upper_bound,
    resource->space,(uint8_t)resource_class);
  args[2] = dxil_module_get_int32_const(m,(int32_t)binding); args[3] = dxil_module_get_int1_const(m,0);
  function = dxil_get_function(m,"dx.op.createHandleFromBinding",DXIL_NONE);
  value = function && args[0] && args[1] && args[2] && args[3] ? dxil_emit_call(m,function,args,4) : NULL;
  return annotate(s,resource_class,id,value);
}
const struct dxil_value *xe_dxil_shader_heap_handle(XeDxilShaderModule *s,
    unsigned resource_class, unsigned id, const struct dxil_value *heap_index, unsigned non_uniform) {
  struct dxil_module *m;
  const struct dxil_func *function;
  const struct dxil_value *args[4], *value;
  if (!s || s->failed || s->finished || s->module.minor_version < 6 || resource_class > 3 ||
      id >= s->resource_counts[resource_class] || !heap_index ||
      dxil_value_get_type(heap_index) != dxil_module_get_int_type(&s->module,32)) return NULL;
  m = &s->module;
  if (resource_class == 3) { m->feats.sampler_descriptor_heap_indexing = 1; s->shader_flags |= (uint64_t)1<<31; }
  else { m->feats.resource_descriptor_heap_indexing = 1; s->shader_flags |= (uint64_t)1<<30; }
  args[0] = dxil_module_get_int32_const(m,218); args[1] = heap_index;
  args[2] = dxil_module_get_int1_const(m,resource_class == 3); args[3] = dxil_module_get_int1_const(m,non_uniform != 0);
  function = dxil_get_function(m,"dx.op.createHandleFromHeap",DXIL_NONE);
  value = function && args[0] && args[2] && args[3] ? dxil_emit_call(m,function,args,4) : NULL;
  return annotate(s,resource_class,id,value);
}

int xe_dxil_shader_geometry(XeDxilShaderModule *s, unsigned primitive, unsigned vertices, unsigned topology) {
  if (!s || s->failed || s->finished) return 0;
  if (s->module.shader_kind != DXIL_GEOMETRY_SHADER || !vertices || vertices > 1024 ||
      (primitive != 1 && primitive != 2 && primitive != 3 && primitive != 6 && primitive != 7) ||
      (topology != 1 && topology != 3 && topology != 5)) { s->failed = 1; return 0; }
  s->stage_tag = DXIL_SHADER_TAG_GS_STATE; s->stage_count = 5;
  s->stage_values[0] = primitive; s->stage_values[1] = vertices;
  s->stage_values[2] = 1; s->stage_values[3] = topology; s->stage_values[4] = 1;
  s->validation.state.psv2.psv1.max_vertex_count = (uint16_t)vertices;
  s->validation.state.psv2.psv1.psv0.gs.input_primitive = primitive;
  s->validation.state.psv2.psv1.psv0.gs.output_toplology = topology;
  s->validation.state.psv2.psv1.psv0.gs.output_stream_mask = 1;
  return 1;
}

static int domain_patch_array(XeDxilShaderModule *s, const char *name, unsigned kind,
    unsigned system, unsigned row, unsigned column, unsigned rows) {
  struct dxil_module *m = &s->module; struct dxil_signature_record *record;
  int id; unsigned i;
  id = xe_dxil_shader_signature(s,2,name,0,kind,system,row,column,1,0);
  if (id < 0 || m->sem_index_table.size+rows-1 > 128) return 0;
  record = &m->patch_consts[id]; record->num_elements = rows;
  record->elements[0].always_reads_mask = 0;
  for (i = 1; i < rows; ++i) {
    record->elements[i] = record->elements[0]; record->elements[i].reg = row+i;
    record->elements[i].semantic_index = i; m->sem_index_table.data[m->sem_index_table.size++] = i;
  }
  m->psv_patch_consts[id].rows = (uint8_t)rows;
  if (m->num_psv_patch_consts < row+rows) m->num_psv_patch_consts = row+rows;
  return 1;
}
int xe_dxil_shader_domain(XeDxilShaderModule *s, unsigned domain, unsigned points) {
  if (!s || s->failed || s->finished) return 0;
  if (s->module.shader_kind != DXIL_DOMAIN_SHADER || domain < 1 || domain > 3 || !points || points > 32) { s->failed = 1; return 0; }
  s->stage_tag = DXIL_SHADER_TAG_DS_STATE; s->stage_count = 2;
  s->stage_values[0] = domain; s->stage_values[1] = points;
  s->validation.state.psv2.psv1.psv0.ds.input_control_point_count = points;
  s->validation.state.psv2.psv1.psv0.ds.tessellator_domain = domain;
  if (domain == 2 || domain == 3) {
    unsigned edges = domain == 2 ? 3 : 4;
    if (!domain_patch_array(s,"SV_TessFactor",DXIL_SEM_TESS_FACTOR,domain == 2 ? DXIL_PROG_SEM_FINAL_TRI_EDGE_TESSFACTOR : DXIL_PROG_SEM_FINAL_QUAD_EDGE_TESSFACTOR,0,3,edges) ||
        !domain_patch_array(s,"SV_InsideTessFactor",DXIL_SEM_INSIDE_TESS_FACTOR,domain == 2 ? DXIL_PROG_SEM_FINAL_TRI_INSIDE_EDGE_TESSFACTOR : DXIL_PROG_SEM_FINAL_QUAD_INSIDE_EDGE_TESSFACTOR,edges,domain == 2 ? 0 : 3,domain == 2 ? 1 : 2)) return 0;
  }
  return 1;
}
int xe_dxil_shader_threads(XeDxilShaderModule *s, unsigned x, unsigned y, unsigned z) {
  if (!s || s->failed || s->finished) return 0;
  if (s->module.shader_kind != DXIL_COMPUTE_SHADER || !x || !y || !z ||
      x > 1024 || y > 1024 || z > 64 || x * y * z > 1024) { s->failed = 1; return 0; }
  s->stage_tag = DXIL_SHADER_TAG_NUM_THREADS; s->stage_count = 3;
  s->stage_values[0] = x; s->stage_values[1] = y; s->stage_values[2] = z;
  s->validation.state.psv2.num_threads_x = x;
  s->validation.state.psv2.num_threads_y = y;
  s->validation.state.psv2.num_threads_z = z;
  return 1;
}

static int named_version(XeDxilShaderModule *s, const char *name, unsigned major, unsigned minor) {
  const struct dxil_mdnode *values[2], *node;
  values[0] = metadata_int(s, major); values[1] = metadata_int(s, minor);
  node = dxil_get_metadata_node(&s->module, values, 2);
  return node && dxil_add_metadata_named_node(&s->module, name, &node, 1);
}

int xe_dxil_shader_finish(XeDxilShaderModule *s, XeDxilShaderBinary *binary) {
  static const char *models[] = {"ps","vs","gs","hs","ds","cs"};
  const struct dxil_mdnode *values[5], *bindings, *properties = NULL, *node;
  struct dxil_container container;
  struct blob output;
  struct dxil_module *m;
  unsigned i, resources = 0, dependency_words;
  int success = 0;
  if (!s || !binary || s->failed || s->finished || binary->data || binary->size) return 0;
  m = &s->module; s->finished = 1;
  dxil_container_init(&container); blob_init(&output);
  if (!named_version(s, "dx.version", 1, m->minor_version) || !named_version(s, "dx.valver", 1, 6)) goto done;
  values[0] = dxil_get_metadata_string(m, models[m->shader_kind]);
  values[1] = metadata_int(s, 6); values[2] = metadata_int(s, m->minor_version);
  node = dxil_get_metadata_node(m, values, 3);
  if (!node || !dxil_add_metadata_named_node(m, "dx.shaderModel", &node, 1)) goto done;
  for (i = 0; i < 4; ++i) {
    resources += s->resource_counts[i];
    values[i] = s->resource_counts[i] ? dxil_get_metadata_node(m, s->resource_nodes[i], s->resource_counts[i]) : NULL;
    if (s->resource_counts[i] && !values[i]) goto done;
  }
  bindings = resources ? dxil_get_metadata_node(m, values, 4) : NULL;
  if (resources && (!bindings || !dxil_add_metadata_named_node(m, "dx.resources", &bindings, 1))) goto done;
  if (s->stage_count) {
    for (i = 0; i < s->stage_count; ++i) values[i] = metadata_int(s, s->stage_values[i]);
    node = dxil_get_metadata_node(m, values, s->stage_count);
    values[0] = metadata_int(s, s->stage_tag); values[1] = node;
    values[2] = metadata_int(s, DXIL_SHADER_TAG_FLAGS);
    values[3] = dxil_get_metadata_int64(m, s->shader_flags | (s->raw_buffers ? 16 : 0));
    properties = dxil_get_metadata_node(m, values, 4);
    if (!node || !properties) goto done;
  } else {
    values[0] = metadata_int(s, DXIL_SHADER_TAG_FLAGS);
    values[1] = dxil_get_metadata_int64(m, s->shader_flags | (s->raw_buffers ? 16 : 0));
    properties = dxil_get_metadata_node(m, values, 2);
    if (!properties) goto done;
  }
  values[0] = dxil_get_metadata_func(m, m->cur_emitting_func->func);
  values[1] = dxil_get_metadata_string(m, "main"); values[2] = get_signatures(m);
  values[3] = bindings; values[4] = properties;
  node = dxil_get_metadata_node(m, values, 5);
  if (!node || !dxil_add_metadata_named_node(m, "dx.entryPoints", &node, 1) || s->failed || !dxil_emit_module(m)) goto done;
  s->validation.state.psv2.psv1.sig_input_elements = (uint8_t)m->num_sig_inputs;
  s->validation.state.psv2.psv1.sig_output_elements = (uint8_t)m->num_sig_outputs;
  s->validation.state.psv2.psv1.sig_patch_const_or_prim_elements = (uint8_t)m->num_sig_patch_consts;
  if (m->shader_kind == DXIL_HULL_SHADER || m->shader_kind == DXIL_DOMAIN_SHADER)
    s->validation.state.psv2.psv1.sig_patch_const_or_prim_vectors = (uint8_t)m->num_psv_patch_consts;
  /* SM 6.0 has no serialized ViewID metadata. The PSV still reserves the
   * dependency table, with zero masks matching the module's absent state. */
  dependency_words = ((m->num_psv_outputs[0] * 4 + 31) / 32) * m->num_psv_inputs * 4;
  if (dependency_words) {
    m->io_dependency_table[0] = rzalloc_array(m->ralloc_ctx, uint32_t, dependency_words);
    if (!m->io_dependency_table[0]) goto done;
    m->io_dependency_table_size[0] = dependency_words;
  }
  if (m->shader_kind == DXIL_DOMAIN_SHADER && m->num_psv_patch_consts) {
    dependency_words = ((m->num_psv_outputs[0]*4+31)/32)*m->num_psv_patch_consts*4;
    if (dependency_words) {
      m->io_dependency_table[1] = rzalloc_array(m->ralloc_ctx,uint32_t,dependency_words);
      if (!m->io_dependency_table[1]) goto done;
      m->io_dependency_table_size[1] = dependency_words;
    }
  }
  if (!dxil_container_add_features(&container, &m->feats) ||
      !dxil_container_add_io_signature(&container, DXIL_ISG1, m->num_sig_inputs, m->inputs, 0) ||
      !dxil_container_add_io_signature(&container, DXIL_OSG1, m->num_sig_outputs, m->outputs, 0) ||
      (m->num_sig_patch_consts && !dxil_container_add_io_signature(&container, DXIL_PSG1, m->num_sig_patch_consts, m->patch_consts, 0)) ||
      !dxil_container_add_state_validation(&container, m, &s->validation) ||
      !dxil_container_add_module(&container, m) || !dxil_container_write(&container, &output)) goto done;
  binary->data = (unsigned char *)output.data; binary->size = output.size;
  output.data = NULL; output.size = 0; success = 1;
done:
  blob_finish(&output); dxil_container_finish(&container);
  return success;
}

void xe_dxil_shader_binary_free(XeDxilShaderBinary *binary) {
  if (!binary) return;
  free(binary->data); binary->data = NULL; binary->size = 0;
}
void xe_dxil_shader_binary_delete(XeDxilShaderBinary *binary) {
  if (!binary) return;
  xe_dxil_shader_binary_free(binary); free(binary);
}
