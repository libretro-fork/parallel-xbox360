/* Native geometry stage generator fixture. MSVC C89; BSD license. */
#include "guest_geometry.h"
#include "stage_layout.h"
#include "guest_alu.h"
#include <dxil_module.h>
#include <dxil_function.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned kill_mode;

static int vertex_shader(const XeDxilGeometryOptions *o, XeDxilShaderBinary *binary) {
  XeDxilShaderModule *shader = xe_dxil_shader_create(DXIL_VERTEX_SHADER);
  struct dxil_module *m;
  const struct dxil_func *load, *store;
  XeDxilValue args[5], position[4], value;
  unsigned i, j, row = XE_DXIL_GUEST_POSITION_ROW, culls = o->cull_planes + !!o->vertex_kill_and;
  int id, result = 0;
  if (!shader) return 0;
  m = xe_dxil_shader_module(shader);
  if (xe_dxil_shader_signature(shader, 0, "POSITION", 0, DXIL_SEM_ARBITRARY, 0, 0, 0, 4, 0) < 0) goto done;
  load = dxil_get_function(m, "dx.op.loadInput", DXIL_F32);
  store = dxil_get_function(m, "dx.op.storeOutput", DXIL_F32);
  if (!load || !store) goto done;
  args[0] = dxil_module_get_int32_const(m, 4); args[1] = dxil_module_get_int32_const(m, 0); args[2] = args[1];
  args[4] = dxil_module_get_undef(m, dxil_module_get_int_type(m, 32));
  for (i = 0; i < 4; ++i) {
    args[3] = dxil_module_get_int8_const(m, (int8_t)i);
    position[i] = dxil_emit_call(m, load, args, 5);
    if (kill_mode == 1 && i == 0) {
      args[3] = dxil_module_get_int32_const(m, 0x7fc00000);
      position[i] = dxil_emit_cast(m, DXIL_CAST_BITCAST, dxil_module_get_float_type(m, 32), args[3]);
    }
    if (kill_mode == 3 && o->type == 4 && i < 2) position[i] = dxil_module_get_float_const(m, 0.0f);
    if (!position[i]) goto done;
  }
  /* Match all native GS input semantics, including packed clip/cull rows. */
  if (xe_dxil_shader_signature(shader, 1, "SV_Position", 0, DXIL_SEM_POSITION,
      DXIL_PROG_SEM_POSITION, row++, 0, 4, 0) < 0) goto done;
  for (i = 0; i < o->clip_planes; i += 4) {
    j = o->clip_planes - i; if (j > 4) j = 4;
    if (xe_dxil_shader_signature(shader, 1, "SV_ClipDistance", i / 4, DXIL_SEM_CLIP_DISTANCE,
        DXIL_PROG_SEM_CLIP_DISTANCE, row++, 0, j, 0) < 0) goto done;
  }
  for (i = 0; i < culls; i += 4) {
    j = culls - i; if (j > 4) j = 4;
    if (xe_dxil_shader_signature(shader, 1, "SV_CullDistance", i / 4, DXIL_SEM_CULL_DISTANCE,
        DXIL_PROG_SEM_CULL_DISTANCE, XE_DXIL_GUEST_DISTANCE_ROW + (o->clip_planes + i) / 4, (o->clip_planes + i) & 3, j, 0) < 0) goto done;
  }
  row = 0;
  for (i = 0; i < o->interpolators; ++i) if (xe_dxil_shader_signature(shader, 1, "TEXCOORD", i,
      DXIL_SEM_ARBITRARY, 0, row++, 0, 4, 0) < 0) goto done;
  if (o->point_size && xe_dxil_shader_signature(shader, 1, "TEXCOORD", o->interpolators,
      DXIL_SEM_ARBITRARY, 0, row++, 0, 1, 0) < 0) goto done;
  for (id = 0; (unsigned)id < m->num_sig_outputs; ++id) {
    args[0] = dxil_module_get_int32_const(m, 5); args[1] = dxil_module_get_int32_const(m, id);
    args[2] = dxil_module_get_int32_const(m, 0);
    for (j = 0; j < (m->psv_outputs[id].cols_and_start & 15u); ++j) {
      args[3] = dxil_module_get_int8_const(m, (int8_t)j);
      if (m->psv_outputs[id].semantic_kind == DXIL_SEM_POSITION) value = position[j];
      else if (m->psv_outputs[id].semantic_kind == DXIL_SEM_ARBITRARY)
        value = dxil_module_get_float_const(m, m->outputs[id].elements[0].semantic_index == o->interpolators ?
          kill_mode == 3 ? 0.0f : -1.0f : (float)(j + 1));
      else value = dxil_module_get_float_const(m, kill_mode == 2 && !j &&
        !m->outputs[id].elements[0].semantic_index && m->psv_outputs[id].semantic_kind == DXIL_SEM_CULL_DISTANCE ? -1.0f : 1.0f);
      args[4] = value;
      if (!dxil_emit_call_void(m, store, args, 5)) goto done;
    }
  }
  if (!dxil_emit_ret_void(m)) goto done;
  result = xe_dxil_shader_finish(shader, binary);
done:
  xe_dxil_shader_destroy(shader);
  return result;
}

static int write_binary(const char *path, XeDxilShaderBinary *binary) {
  FILE *file = fopen(path, "wb");
  int result;
  if (!file) return 0;
  result = fwrite(binary->data, 1, binary->size, file) == binary->size;
  if (fclose(file)) result = 0;
  return result;
}
int main(int argc, char **argv) {
  XeDxilGeometryOptions options;
  XeDxilShaderBinary binary;
  unsigned variant;
  int result;
  char *vertex_path;
  size_t length;
  if (argc != 4 && argc != 5) return 2;
  kill_mode = argc == 5 ? (unsigned)strtoul(argv[4], NULL, 10) : 0;
  if (kill_mode > 3) return 2;
  memset(&options, 0, sizeof(options)); memset(&binary, 0, sizeof(binary));
  options.type = (unsigned)strtoul(argv[2], NULL, 10);
  variant = (unsigned)strtoul(argv[3], NULL, 10);
  if (variant > 2) return 2;
  options.interpolators = variant ? 16 : 1;
  options.clip_planes = variant == 1 ? 6 : 0;
  options.cull_planes = variant == 2 ? 6 : 0;
  options.vertex_kill_and = variant != 0;
  options.point_size = variant != 0;
  options.point_coordinates = variant != 0;
  options.point_diameter_word = 20; options.point_radius_word = 22;
  if (!xe_dxil_build_geometry(&options, &binary)) return 1;
  result = !write_binary(argv[1], &binary);
  xe_dxil_shader_binary_free(&binary);
  if (result || !vertex_shader(&options, &binary)) return 1;
  length = strlen(argv[1]);
  vertex_path = (char *)malloc(length + 4);
  if (!vertex_path) { xe_dxil_shader_binary_free(&binary); return 1; }
  memcpy(vertex_path, argv[1], length); memcpy(vertex_path + length, ".vs", 4);
  result = !write_binary(vertex_path, &binary);
  free(vertex_path); xe_dxil_shader_binary_free(&binary);
  return result;
}
