/* Standalone validation fixture for the owned raw DXIL writer. C89. */
#include "dxil_module.h"
#include "dxil_container.h"
#include "dxil_function.h"
#include <stdio.h>
#ifdef XE_DXIL_EDRAM_TEST
int xe_dxil_edram_format_test(struct dxil_module *module, const char *path);
#endif
#ifdef XE_DXIL_TEXTURE_TEST
int xe_dxil_texture_test(struct dxil_module *module, const char *path);
int xe_dxil_texture_pixel_test(struct dxil_module *module, const struct dxil_value *values[4]);
#endif
#ifdef XE_DXIL_RESOURCES_TEST
int xe_dxil_resources_test(struct dxil_module *module, const char *path);
#endif
#ifdef XE_DXIL_ALU_TEST
int xe_dxil_alu_test(struct dxil_module *module, const char *path);
#endif
#ifdef XE_DXIL_FLOW_TEST
int xe_dxil_flow_test(struct dxil_module *module, const char *path, unsigned index);
#endif

#ifdef XE_DXIL_VERTEX_TEST
int xe_dxil_vertex_test(struct dxil_module *module, const char *path);
#endif

static int arithmetic_and_control_flow(struct dxil_module *module,
                                       struct dxil_func_def *function)
{
  const struct dxil_func *thread_id;
  const struct dxil_value *args[9];
  const struct dxil_value *handle;
  const struct dxil_value *id;
  const struct dxil_value *condition;
  const struct dxil_value *left;
  const struct dxil_value *right;
  const struct dxil_value *incoming[2];
  unsigned blocks[2] = {1, 2};
  struct dxil_instr *phi;
  thread_id = dxil_get_function(module, "dx.op.threadId", DXIL_I32);
  args[0] = dxil_module_get_int32_const(module, 93);
  args[1] = dxil_module_get_int32_const(module, 0);
  if (!thread_id || !args[0] || !args[1]) return 0;
  id = dxil_emit_call(module, thread_id, args, 2);
  if (!id) return 0;
  args[0] = dxil_module_get_int32_const(module, 57);
  args[1] = dxil_module_get_int8_const(module, DXIL_RESOURCE_CLASS_UAV);
  args[2] = dxil_module_get_int32_const(module, 0);
  args[3] = args[2];
  args[4] = dxil_module_get_int1_const(module, 0);
  thread_id = dxil_get_function(module, "dx.op.createHandle", DXIL_NONE);
  if (!thread_id) return 0;
  handle = dxil_emit_call(module, thread_id, args, 5);
  if (!handle) return 0;
  condition = dxil_emit_cmp(module, DXIL_ICMP_ULT, id,
                            dxil_module_get_int32_const(module, 16));
  if (!condition || !dxil_emit_branch(module, condition, 1, 2)) return 0;
  function->basic_block_ids[1] = function->curr_block;
  left = dxil_emit_binop(module, DXIL_BINOP_ADD, id,
                         dxil_module_get_int32_const(module, 1), 0);
  if (!left || !dxil_emit_branch(module, NULL, 3, 0)) return 0;
  function->basic_block_ids[2] = function->curr_block;
  right = dxil_emit_binop(module, DXIL_BINOP_MUL, id,
                          dxil_module_get_int32_const(module, 3), 0);
  if (!right || !dxil_emit_branch(module, NULL, 3, 0)) return 0;
  function->basic_block_ids[3] = function->curr_block;
  phi = dxil_emit_phi(module, dxil_module_get_int_type(module, 32));
  incoming[0] = left; incoming[1] = right;
  if (!phi || !dxil_phi_add_incoming(phi, incoming, blocks, 2)) return 0;
  left = dxil_emit_cast(module, DXIL_CAST_UITOFP,
                        dxil_module_get_float_type(module, 32),
                        dxil_instr_get_return_value(phi));
  if (!left) return 0;
  right = dxil_emit_binop(module, DXIL_BINOP_ADD, left,
                          dxil_module_get_float_const(module, 0.25f), 0);
  if (!right) return 0;
  args[0] = dxil_module_get_int32_const(module, 69);
  args[1] = handle;
  args[2] = dxil_emit_binop(module, DXIL_BINOP_MUL, id,
                            dxil_module_get_int32_const(module, 4), 0);
  args[3] = dxil_module_get_undef(module, dxil_module_get_int_type(module, 32));
  args[4] = dxil_emit_cast(module, DXIL_CAST_BITCAST,
                            dxil_module_get_int_type(module, 32), right);
  args[5] = args[3]; args[6] = args[3]; args[7] = args[3];
  args[8] = dxil_module_get_int8_const(module, 1);
  thread_id = dxil_get_function(module, "dx.op.bufferStore", DXIL_I32);
  return thread_id && args[2] && args[4] &&
    dxil_emit_call_void(module, thread_id, args, 9) && dxil_emit_ret_void(module);
}

static const struct dxil_mdnode *resources(struct dxil_module *module, int inputs)
{
  const struct dxil_mdnode *fields[11];
  const struct dxil_mdnode *resource;
  const struct dxil_mdnode *uavs;
  const struct dxil_mdnode *srvs = NULL;
  const struct dxil_mdnode *cbvs = NULL;
  const struct dxil_mdnode *samplers = NULL;
  const struct dxil_mdnode *pair[2];
  const struct dxil_type *type;
  const struct dxil_type *element;
  element = dxil_module_get_int_type(module, 32);
  type = dxil_module_get_struct_type(module, "RWByteAddressBuffer", &element, 1);
  type = dxil_module_get_pointer_type(module, type);
  fields[0] = dxil_get_metadata_int32(module, 0);
  fields[1] = dxil_get_metadata_value(module, type, dxil_module_get_undef(module, type));
  fields[2] = dxil_get_metadata_string(module, "output");
  fields[3] = fields[0]; fields[4] = fields[0];
  fields[5] = dxil_get_metadata_int32(module, 1);
  fields[6] = dxil_get_metadata_int32(module, DXIL_RESOURCE_KIND_RAW_BUFFER);
  fields[7] = dxil_get_metadata_int1(module, 0);
  fields[8] = fields[7]; fields[9] = fields[7]; fields[10] = NULL;
  resource = dxil_get_metadata_node(module, fields, 11);
  uavs = dxil_get_metadata_node(module, &resource, 1);
  if (inputs == 1) {
    type = dxil_module_get_struct_type(module, "ByteAddressBuffer", &element, 1);
    type = dxil_module_get_pointer_type(module, type);
    fields[1] = dxil_get_metadata_value(module, type, dxil_module_get_undef(module, type));
    fields[2] = dxil_get_metadata_string(module, "shared_memory");
    fields[7] = dxil_get_metadata_int32(module, 0); fields[8] = NULL;
    resource = dxil_get_metadata_node(module, fields, 9);
    srvs = dxil_get_metadata_node(module, &resource, 1);
    element = dxil_module_get_array_type(module, element, 192);
    type = dxil_module_get_struct_type(module, "FetchConstants", &element, 1);
    type = dxil_module_get_pointer_type(module, type);
    fields[1] = dxil_get_metadata_value(module, type, dxil_module_get_undef(module, type));
    fields[2] = dxil_get_metadata_string(module, "fetch_constants");
    fields[3] = dxil_get_metadata_int32(module, 1); fields[4] = dxil_get_metadata_int32(module, 4);
    fields[6] = dxil_get_metadata_int32(module, 768); fields[7] = NULL;
    resource = dxil_get_metadata_node(module, fields, 8);
    cbvs = dxil_get_metadata_node(module, &resource, 1);
    if (!srvs || !cbvs) return NULL;
  } else if (inputs >= 2) {
    element = dxil_module_get_vector_type(module, dxil_module_get_float_type(module, 32), 4);
    type = dxil_module_get_struct_type(module, "Texture2D<float4>", &element, 1);
    type = dxil_module_get_pointer_type(module, type);
    fields[1] = dxil_get_metadata_value(module, type, dxil_module_get_undef(module, type));
    fields[2] = dxil_get_metadata_string(module, "texture");
    fields[6] = dxil_get_metadata_int32(module, DXIL_RESOURCE_KIND_TEXTURE2D);
    fields[7] = dxil_get_metadata_int32(module, 0);
    pair[0] = dxil_get_metadata_int32(module, 0); pair[1] = dxil_get_metadata_int32(module, DXIL_COMP_TYPE_F32);
    fields[8] = dxil_get_metadata_node(module, pair, 2);
    resource = dxil_get_metadata_node(module, fields, 9);
    srvs = dxil_get_metadata_node(module, &resource, 1);
    element = dxil_module_get_int_type(module, 32);
    type = dxil_module_get_struct_type(module, "SamplerState", &element, 1);
    type = dxil_module_get_pointer_type(module, type);
    fields[1] = dxil_get_metadata_value(module, type, dxil_module_get_undef(module, type));
    fields[2] = dxil_get_metadata_string(module, "point");
    fields[6] = dxil_get_metadata_int32(module, 0); fields[7] = NULL;
    pair[0] = dxil_get_metadata_node(module, fields, 8);
    fields[0] = dxil_get_metadata_int32(module, 1); fields[4] = fields[0];
    fields[2] = dxil_get_metadata_string(module, "linear");
    pair[1] = dxil_get_metadata_node(module, fields, 8);
    samplers = dxil_get_metadata_node(module, pair, 2);
    if (!srvs || !samplers) return NULL;
  }
  fields[0] = srvs; fields[1] = inputs == 3 ? NULL : uavs; fields[2] = cbvs; fields[3] = samplers;
  return dxil_get_metadata_node(module, fields, 4);
}

static int version(struct dxil_module *module, const char *name, int major, int minor)
{
  const struct dxil_mdnode *values[2];
  const struct dxil_mdnode *node;
  values[0] = dxil_get_metadata_int32(module, major);
  values[1] = dxil_get_metadata_int32(module, minor);
  node = dxil_get_metadata_node(module, values, 2);
  return node && dxil_add_metadata_named_node(module, name, &node, 1);
}

static int constant_output(struct dxil_module *module, int texture)
{
  const struct dxil_func *store;
  const struct dxil_value *args[5];
  const struct dxil_value *sampled[4];
  struct dxil_signature_record *record;
  struct dxil_psv_signature_element *psv;
  unsigned component;
  int vertex;
  vertex = module->shader_kind == DXIL_VERTEX_SHADER;
  record = &module->outputs[0]; psv = &module->psv_outputs[0];
  record->num_elements = 1;
  record->name = ralloc_strdup(module->ralloc_ctx, vertex ? "SV_Position" : "SV_Target");
  if (!record->name) return 0;
  record->sig_comp_type = DXIL_COMP_TYPE_F32;
  record->elements[0].mask = 15;
  record->elements[0].system_value = vertex ? DXIL_PROG_SEM_POSITION : DXIL_PROG_SEM_TARGET;
  record->elements[0].comp_type = DXIL_PROG_SIG_COMP_TYPE_FLOAT32;
  psv->rows = 1; psv->cols_and_start = 68;
  psv->semantic_kind = vertex ? DXIL_SEM_POSITION : DXIL_SEM_TARGET;
  psv->component_type = DXIL_PROG_SIG_COMP_TYPE_FLOAT32;
  module->num_sig_outputs = 1; module->num_psv_outputs[0] = 1;
  module->sem_index_table.size = 1;
  store = dxil_get_function(module, "dx.op.storeOutput", DXIL_F32);
  if (!store) return 0;
  args[0] = dxil_module_get_int32_const(module, 5);
  args[1] = dxil_module_get_int32_const(module, 0); args[2] = args[1];
#ifdef XE_DXIL_TEXTURE_TEST
  if (texture && !xe_dxil_texture_pixel_test(module, sampled)) return 0;
#else
  (void)texture; (void)sampled;
#endif
  for (component = 0; component < 4; ++component) {
    args[3] = dxil_module_get_int8_const(module, (int8_t)component);
    args[4] = dxil_module_get_float_const(module, (float)component * 0.25f);
#ifdef XE_DXIL_TEXTURE_TEST
    if (texture) args[4] = sampled[component];
#endif
    if (!dxil_emit_call_void(module, store, args, 5)) return 0;
  }
  return dxil_emit_ret_void(module);
}

int main(int argc, char **argv)
{
  void *arena;
  struct dxil_module module;
  struct dxil_container container;
  struct dxil_validation_state validation;
  struct dxil_resource_v1 resource[4];
  struct blob output;
  struct dxil_func_def *function;
  const struct dxil_type *type;
  const struct dxil_mdnode *values[5];
  const struct dxil_mdnode *shader_model;
  const struct dxil_mdnode *threads;
  const struct dxil_mdnode *properties;
  const struct dxil_mdnode *entry;
  const struct dxil_mdnode *bindings;
  const char *attributes[1] = {NULL};
  FILE *file;
  int result = 1;
  const char *stage = "cs";
  int inputs = 0;
  if (argc != 2 && argc != 3
#ifdef XE_DXIL_FLOW_TEST
      && argc != 4
#endif
      ) return 2;
  if (argc >= 3) stage = argv[2];
  if (strcmp(stage, "cs") && strcmp(stage, "vs") && strcmp(stage, "ps")
#ifdef XE_DXIL_EDRAM_TEST
      && strcmp(stage, "edram")
#endif
#ifdef XE_DXIL_ALU_TEST
      && strcmp(stage, "alu")
#endif
#ifdef XE_DXIL_VERTEX_TEST
      && strcmp(stage, "vertex")
#endif
#ifdef XE_DXIL_RESOURCES_TEST
      && strcmp(stage, "resources")
#endif
#ifdef XE_DXIL_TEXTURE_TEST
      && strcmp(stage, "texture")
      && strcmp(stage, "texture-ps")
#endif
#ifdef XE_DXIL_FLOW_TEST
      && strcmp(stage, "flow")
#endif
      ) return 2;
  inputs = !strcmp(stage, "texture-ps") ? 3 : !strcmp(stage, "texture") ? 2 : !strcmp(stage, "resources");
  arena = ralloc_size(NULL, 0);
  if (!arena) return 1;
  dxil_module_init(&module, arena);
  dxil_container_init(&container);
  blob_init(&output);
  module.shader_kind = DXIL_COMPUTE_SHADER;
  if (!strcmp(stage, "vs")) module.shader_kind = DXIL_VERTEX_SHADER;
  if (!strcmp(stage, "ps")) module.shader_kind = DXIL_PIXEL_SHADER;
  if (inputs == 3) module.shader_kind = DXIL_PIXEL_SHADER;
  module.major_version = 6;
  module.minor_version = 0;
  module.major_validator = 1;
  module.minor_validator = 6;
  module.sem_string_table = _mesa_string_buffer_create(arena, 16);
  if (!module.sem_string_table || !_mesa_string_buffer_append_len(module.sem_string_table, "", 1)) goto done;
  type = dxil_module_add_function_type(&module, dxil_module_get_void_type(&module), NULL, 0);
  function = dxil_add_function_def(&module, "main", type,
      module.shader_kind == DXIL_COMPUTE_SHADER ? 64 : 1, attributes, attributes);
  if (!function) goto done;
  function->basic_block_ids[0] = 0;
  if (module.shader_kind == DXIL_COMPUTE_SHADER) {
#ifdef XE_DXIL_EDRAM_TEST
    if (!strcmp(stage, "edram")) {
      if (!xe_dxil_edram_format_test(&module, argv[1])) goto done;
    } else
#endif
#ifdef XE_DXIL_TEXTURE_TEST
    if (inputs == 2) {
      if (!xe_dxil_texture_test(&module, argv[1])) goto done;
    } else
#endif
#ifdef XE_DXIL_RESOURCES_TEST
    if (inputs == 1) {
      if (!xe_dxil_resources_test(&module, argv[1])) goto done;
    } else
#endif
#ifdef XE_DXIL_FLOW_TEST
    if (!strcmp(stage, "flow")) {
      if (!xe_dxil_flow_test(&module, argv[1], argc == 4 ? (unsigned)strtoul(argv[3], NULL, 10) : 0)) goto done;
    } else
#endif
#ifdef XE_DXIL_ALU_TEST
    if (!strcmp(stage, "alu")) {
      if (!xe_dxil_alu_test(&module, argv[1])) goto done;
    } else
#endif
#ifdef XE_DXIL_VERTEX_TEST
    if (!strcmp(stage, "vertex")) {
      if (!xe_dxil_vertex_test(&module, argv[1])) goto done;
    } else
#endif
    if (!arithmetic_and_control_flow(&module, function)) goto done;
  } else if (!constant_output(&module, inputs == 3)) goto done;
  if (!version(&module, "dx.version", 1, 0) || !version(&module, "dx.valver", 1, 6)) goto done;
  values[0] = dxil_get_metadata_string(&module, module.shader_kind == DXIL_COMPUTE_SHADER ? "cs" :
    module.shader_kind == DXIL_VERTEX_SHADER ? "vs" : "ps");
  values[1] = dxil_get_metadata_int32(&module, 6);
  values[2] = dxil_get_metadata_int32(&module, 0);
  shader_model = dxil_get_metadata_node(&module, values, 3);
  if (!dxil_add_metadata_named_node(&module, "dx.shaderModel", &shader_model, 1)) goto done;
  properties = NULL; bindings = NULL;
  if (module.shader_kind == DXIL_COMPUTE_SHADER) {
  values[0] = dxil_get_metadata_int32(&module, 1);
  values[1] = values[0];
  values[2] = values[0];
  threads = dxil_get_metadata_node(&module, values, 3);
  values[0] = dxil_get_metadata_int32(&module, DXIL_SHADER_TAG_NUM_THREADS);
  values[1] = threads;
  values[2] = dxil_get_metadata_int32(&module, DXIL_SHADER_TAG_FLAGS);
  values[3] = dxil_get_metadata_int64(&module, 16);
  properties = dxil_get_metadata_node(&module, values, 4);
  bindings = resources(&module, inputs);
  if (!bindings || !dxil_add_metadata_named_node(&module, "dx.resources", &bindings, 1)) goto done;
  }
  if (inputs == 3) {
    bindings = resources(&module, inputs);
    if (!bindings || !dxil_add_metadata_named_node(&module, "dx.resources", &bindings, 1)) goto done;
  }
  values[0] = dxil_get_metadata_func(&module, function->func);
  values[1] = dxil_get_metadata_string(&module, "main");
  values[2] = get_signatures(&module);
  values[3] = bindings;
  values[4] = properties;
  entry = dxil_get_metadata_node(&module, values, 5);
  if (!dxil_add_metadata_named_node(&module, "dx.entryPoints", &entry, 1)) goto done;
  if (!dxil_emit_module(&module)) goto done;
  memset(&validation, 0, sizeof(validation));
  memset(&resource, 0, sizeof(resource));
  if (module.shader_kind == DXIL_COMPUTE_SHADER) {
  resource[inputs == 2 ? 3 : inputs ? 2 : 0].v0.resource_type = DXIL_RES_UAV_RAW;
  resource[inputs == 2 ? 3 : inputs ? 2 : 0].resource_kind = DXIL_RESOURCE_KIND_RAW_BUFFER;
  validation.resources.v1 = resource;
  validation.num_resources = inputs == 2 ? 4 : inputs ? 3 : 1;
  if (inputs == 1) {
    resource[1].v0.resource_type = DXIL_RES_SRV_RAW;
    resource[1].resource_kind = DXIL_RESOURCE_KIND_RAW_BUFFER;
    resource[0].v0.resource_type = DXIL_RES_CBV;
    resource[0].resource_kind = DXIL_RESOURCE_KIND_CBUFFER;
    resource[0].v0.space = 1;
    resource[0].v0.lower_bound = 4; resource[0].v0.upper_bound = 4;
  } else if (inputs == 2) {
    resource[0].v0.resource_type = DXIL_RES_SAMPLER;
    resource[0].resource_kind = DXIL_RESOURCE_KIND_SAMPLER;
    resource[1] = resource[0];
    resource[1].v0.lower_bound = 1; resource[1].v0.upper_bound = 1;
    resource[2].v0.resource_type = DXIL_RES_SRV_TYPED;
    resource[2].resource_kind = DXIL_RESOURCE_KIND_TEXTURE2D;
  }
  validation.state.psv2.num_threads_x = 1;
  validation.state.psv2.num_threads_y = 1;
  validation.state.psv2.num_threads_z = 1;
  } else {
    validation.state.psv2.psv1.sig_output_elements = 1;
    if (module.shader_kind == DXIL_VERTEX_SHADER)
      validation.state.psv2.psv1.psv0.vs.output_position_present = 1;
  }
  if (inputs == 3) {
    resource[0].v0.resource_type = DXIL_RES_SAMPLER;
    resource[0].resource_kind = DXIL_RESOURCE_KIND_SAMPLER;
    resource[1] = resource[0];
    resource[1].v0.lower_bound = 1; resource[1].v0.upper_bound = 1;
    resource[2].v0.resource_type = DXIL_RES_SRV_TYPED;
    resource[2].resource_kind = DXIL_RESOURCE_KIND_TEXTURE2D;
    validation.resources.v1 = resource; validation.num_resources = 3;
  }
  validation.state.psv2.psv1.psv0.max_expected_wave_lane_count = UINT32_MAX;
  validation.state.psv2.psv1.shader_stage = (uint8_t)module.shader_kind;
  if (!dxil_container_add_features(&container, &module.feats) ||
      !dxil_container_add_io_signature(&container, DXIL_ISG1, module.num_sig_inputs, module.inputs, 0) ||
      !dxil_container_add_io_signature(&container, DXIL_OSG1, module.num_sig_outputs, module.outputs, 0) ||
      !dxil_container_add_state_validation(&container, &module, &validation) ||
      !dxil_container_add_module(&container, &module) ||
      !dxil_container_write(&container, &output)) goto done;
  file = fopen(argv[1], "wb");
  if (!file) goto done;
  result = fwrite(output.data, 1, output.size, file) == output.size ? 0 : 1;
  if (fclose(file)) result = 1;
done:
  blob_finish(&output);
  dxil_container_finish(&container);
  dxil_module_release(&module);
  ralloc_free(arena);
  return result;
}
