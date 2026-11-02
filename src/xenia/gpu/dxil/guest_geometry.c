/* Direct DXIL point, rectangle, quad and line expansion. MSVC C89.
 * Guest primitive rules follow Xenia's SPIR-V geometry generator.
 * Copyright 2026 Ben Vanik and contributors. BSD license, see LICENSE. */
#include "guest_geometry.h"
#include "guest_resources.h"
#include <dxil_module.h>
#include <dxil_function.h>

typedef struct Geometry {
  XeDxilAlu alu;
  XeDxilShaderModule *shader;
  XeDxilValue position[4][4], interpolator[4][16][4], clip[4][6], cull[4][7], size;
  unsigned input_position, output_position, input_clip[2], output_clip[2], input_cull[2];
  unsigned input_interpolator[16], output_interpolator[16], input_size, output_coordinates;
} Geometry;

static XeDxilValue integer(Geometry *g, unsigned value) {
  return dxil_module_get_int32_const(g->alu.module, (int32_t)value);
}
static XeDxilValue floating(Geometry *g, float value) {
  return dxil_module_get_float_const(g->alu.module, value);
}
static XeDxilValue binary(Geometry *g, enum dxil_bin_opcode op, XeDxilValue a, XeDxilValue b) {
  XeDxilValue value = a && b ? dxil_emit_binop(g->alu.module, op, a, b, 0) : NULL;
  if (!value) g->alu.failed = 1;
  return value;
}
static XeDxilValue compare(Geometry *g, enum dxil_cmp_pred op, XeDxilValue a, XeDxilValue b) {
  XeDxilValue value = a && b ? dxil_emit_cmp(g->alu.module, op, a, b) : NULL;
  if (!value) g->alu.failed = 1;
  return value;
}
static XeDxilValue select_value(Geometry *g, XeDxilValue condition, XeDxilValue a, XeDxilValue b) {
  XeDxilValue value = condition && a && b ? dxil_emit_select(g->alu.module, condition, a, b) : NULL;
  if (!value) g->alu.failed = 1;
  return value;
}
static XeDxilValue input(Geometry *g, unsigned id, unsigned vertex, unsigned component) {
  XeDxilValue args[5], value;
  const struct dxil_func *function = dxil_get_function(g->alu.module, "dx.op.loadInput", DXIL_F32);
  args[0] = integer(g, 4); args[1] = integer(g, id); args[2] = integer(g, 0);
  args[3] = dxil_module_get_int8_const(g->alu.module, (int8_t)component); args[4] = integer(g, vertex);
  value = function ? dxil_emit_call(g->alu.module, function, args, 5) : NULL;
  if (!value) g->alu.failed = 1;
  return value;
}
static int output(Geometry *g, unsigned id, unsigned component, XeDxilValue value) {
  XeDxilValue args[5];
  const struct dxil_func *function = dxil_get_function(g->alu.module, "dx.op.storeOutput", DXIL_F32);
  args[0] = integer(g, 5); args[1] = integer(g, id); args[2] = integer(g, 0);
  args[3] = dxil_module_get_int8_const(g->alu.module, (int8_t)component); args[4] = value;
  return function && value && dxil_emit_call_void(g->alu.module, function, args, 5);
}
static int stream(Geometry *g, int end) {
  XeDxilValue args[2];
  const struct dxil_func *function = dxil_get_function(g->alu.module,
    end ? "dx.op.cutStream" : "dx.op.emitStream", DXIL_NONE);
  args[0] = integer(g, end ? 98 : 97); args[1] = dxil_module_get_int8_const(g->alu.module, 0);
  return function && dxil_emit_call_void(g->alu.module, function, args, 2);
}
static int return_if(Geometry *g, XeDxilValue condition) {
  struct dxil_module *m = g->alu.module;
  unsigned blocks;
  if (!condition || !dxil_function_reserve_blocks(m, 2, &blocks) ||
      !dxil_emit_branch(m, condition, blocks, blocks + 1)) return 0;
  m->cur_emitting_func->basic_block_ids[blocks] = (int)m->cur_emitting_func->curr_block;
  if (!dxil_emit_ret_void(m)) return 0;
  m->cur_emitting_func->basic_block_ids[blocks + 1] = (int)m->cur_emitting_func->curr_block;
  return 1;
}
static int signature(Geometry *g, unsigned direction, const char *name, unsigned index,
                        unsigned kind, unsigned system, unsigned columns, unsigned *id) {
  unsigned row = direction ? g->alu.module->num_psv_outputs[0] : g->alu.module->num_psv_inputs;
  int result = xe_dxil_shader_signature(g->shader, direction, name, index, kind, system, row, 0, columns, 0);
  if (result < 0) return 0;
  *id = (unsigned)result;
  return 1;
}
static XeDxilValue indexed(Geometry *g, XeDxilValue index, XeDxilValue a, XeDxilValue b, XeDxilValue c) {
  return select_value(g, compare(g, DXIL_ICMP_EQ, index, integer(g, 0)), a,
    select_value(g, compare(g, DXIL_ICMP_EQ, index, integer(g, 1)), b, c));
}
static XeDxilValue rectangle_value(Geometry *g, unsigned corner, XeDxilValue indices[3],
                                    XeDxilValue a, XeDxilValue b, XeDxilValue c) {
  if (corner < 3) return indexed(g, indices[corner], a, b, c);
  return binary(g, DXIL_BINOP_ADD,
    binary(g, DXIL_BINOP_SUB, indexed(g, indices[1], a, b, c), indexed(g, indices[0], a, b, c)),
    indexed(g, indices[2], a, b, c));
}

int xe_dxil_build_geometry(const XeDxilGeometryOptions *o, XeDxilShaderBinary *binary_out) {
  Geometry g;
  XeDxilValue condition, negative, invalid, radius[2], diameter[2], handle;
  XeDxilValue indices[3], edge[3], a, b, delta[2], length, offsets[2], value;
  XeDxilValue position[4];
  unsigned vertices, primitive, culls, i, j, k, corner, source;
  int result = 0, id;
  if (!o || !binary_out || o->type < 1 || o->type > 4 || o->interpolators > 16 ||
      o->clip_planes > 6 || o->cull_planes > 6 || (o->clip_planes && o->cull_planes) ||
      o->point_diameter_word >= 16383 || o->point_radius_word >= 16383) return 0;
  memset(&g, 0, sizeof(g));
  g.shader = xe_dxil_shader_create(DXIL_GEOMETRY_SHADER);
  if (!g.shader) return 0;
  xe_dxil_alu_init(&g.alu, xe_dxil_shader_module(g.shader));
  vertices = o->type == 1 ? 1 : o->type == 2 ? 3 : o->type == 3 ? 4 : 2;
  primitive = o->type == 1 ? 1 : o->type == 2 ? 3 : o->type == 3 ? 6 : 2;
  if (!xe_dxil_shader_geometry(g.shader, primitive, 4, DXIL_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP) ||
      !signature(&g, 0, "SV_Position", 0, DXIL_SEM_POSITION, DXIL_PROG_SEM_POSITION, 4, &g.input_position) ||
      !signature(&g, 1, "SV_Position", 0, DXIL_SEM_POSITION, DXIL_PROG_SEM_POSITION, 4, &g.output_position)) goto done;
  for (i = 0; i < o->clip_planes; i += 4) {
    k = o->clip_planes - i; if (k > 4) k = 4;
    if (!signature(&g, 0, "SV_ClipDistance", i / 4, DXIL_SEM_CLIP_DISTANCE,
        DXIL_PROG_SEM_CLIP_DISTANCE, k, &g.input_clip[i / 4]) ||
        !signature(&g, 1, "SV_ClipDistance", i / 4, DXIL_SEM_CLIP_DISTANCE,
        DXIL_PROG_SEM_CLIP_DISTANCE, k, &g.output_clip[i / 4])) goto done;
  }
  culls = o->cull_planes + !!o->vertex_kill_and;
  for (i = 0; i < culls; i += 4) {
    k = culls - i; if (k > 4) k = 4;
    id = xe_dxil_shader_signature(g.shader, 0, "SV_CullDistance", i / 4, DXIL_SEM_CULL_DISTANCE,
      DXIL_PROG_SEM_CULL_DISTANCE, 1 + (o->clip_planes + i) / 4, (o->clip_planes + i) & 3, k, 0);
    if (id < 0) goto done;
    g.input_cull[i / 4] = (unsigned)id;
  }
  for (i = 0; i < o->interpolators; ++i) {
    if (!signature(&g, 0, "TEXCOORD", i, DXIL_SEM_ARBITRARY, 0, 4, &g.input_interpolator[i]) ||
        !signature(&g, 1, "TEXCOORD", i, DXIL_SEM_ARBITRARY, 0, 4, &g.output_interpolator[i])) goto done;
  }
  if (o->point_size && !signature(&g, 0, "TEXCOORD", o->interpolators, DXIL_SEM_ARBITRARY,
      0, 1, &g.input_size)) goto done;
  if (o->point_coordinates && !signature(&g, 1, "TEXCOORD", o->interpolators, DXIL_SEM_ARBITRARY,
      0, 2, &g.output_coordinates)) goto done;
  invalid = dxil_module_get_int1_const(g.alu.module, 0);
  for (i = 0; i < vertices; ++i) {
    for (j = 0; j < 4; ++j) {
      g.position[i][j] = input(&g, g.input_position, i, j);
      invalid = binary(&g, DXIL_BINOP_OR, invalid, compare(&g, DXIL_FCMP_UNO, g.position[i][j], g.position[i][j]));
    }
    for (j = 0; j < o->interpolators; ++j) for (k = 0; k < 4; ++k)
      g.interpolator[i][j][k] = input(&g, g.input_interpolator[j], i, k);
    for (j = 0; j < o->clip_planes; ++j) g.clip[i][j] = input(&g, g.input_clip[j / 4], i, j & 3);
    for (j = 0; j < culls; ++j) g.cull[i][j] = input(&g, g.input_cull[j / 4], i, j & 3);
  }
  for (j = 0; j < culls; ++j) {
    negative = dxil_module_get_int1_const(g.alu.module, 1);
    for (i = 0; i < vertices; ++i)
      negative = binary(&g, DXIL_BINOP_AND, negative, compare(&g, DXIL_FCMP_OLT, g.cull[i][j], floating(&g, 0.0f)));
    invalid = binary(&g, DXIL_BINOP_OR, invalid, negative);
  }
  if (g.alu.failed || !return_if(&g, invalid)) goto done;
  for (i = 0; i < 2; ++i) { radius[i] = NULL; offsets[i] = NULL; }
  if (o->type == 1 || o->type == 4) {
    if (xe_dxil_shader_resource(g.shader, 2, DXIL_RESOURCE_KIND_CBUFFER, 1, 0, 1, 4096) != 0) goto done;
    handle = xe_dxil_resource_handle(&g.alu, XE_DXIL_RESOURCE_CBV, 0, 0);
    for (i = 0; i < 2; ++i) {
      value = xe_dxil_cbuffer_word(&g.alu, handle, integer(&g, o->point_radius_word + i));
      radius[i] = value ? dxil_emit_cast(g.alu.module, DXIL_CAST_BITCAST,
        dxil_module_get_float_type(g.alu.module, 32), value) : NULL;
    }
    if (o->type == 1) {
      if (o->point_size) g.size = input(&g, g.input_size, 0, 0);
      for (i = 0; i < 2; ++i) {
        value = xe_dxil_cbuffer_word(&g.alu, handle, integer(&g, o->point_diameter_word + i));
        diameter[i] = value ? dxil_emit_cast(g.alu.module, DXIL_CAST_BITCAST,
          dxil_module_get_float_type(g.alu.module, 32), value) : NULL;
        if (o->point_size) diameter[i] = select_value(&g, compare(&g, DXIL_FCMP_OGE,
          g.size, floating(&g, 0.0f)), g.size, diameter[i]);
        radius[i] = binary(&g, DXIL_BINOP_MUL,
          binary(&g, DXIL_BINOP_MUL, diameter[i], radius[i]), g.position[0][3]);
      }
      condition = binary(&g, DXIL_BINOP_OR, compare(&g, DXIL_FCMP_ULE, diameter[0], floating(&g, 0.0f)),
        compare(&g, DXIL_FCMP_ULE, diameter[1], floating(&g, 0.0f)));
      if (!return_if(&g, condition)) goto done;
    } else {
      for (i = 0; i < 2; ++i) {
        a = binary(&g, DXIL_BINOP_MUL, g.position[0][i],
          xe_dxil_alu_scalar(&g.alu, XE_DXIL_S_RCP, g.position[0][3], NULL, 0));
        b = binary(&g, DXIL_BINOP_MUL, g.position[1][i],
          xe_dxil_alu_scalar(&g.alu, XE_DXIL_S_RCP, g.position[1][3], NULL, 0));
        delta[i] = binary(&g, DXIL_BINOP_MUL, binary(&g, DXIL_BINOP_SUB, b, a),
          xe_dxil_alu_scalar(&g.alu, XE_DXIL_S_RCP, radius[i], NULL, 0));
      }
      length = binary(&g, DXIL_BINOP_ADD, binary(&g, DXIL_BINOP_MUL, delta[0], delta[0]),
        binary(&g, DXIL_BINOP_MUL, delta[1], delta[1]));
      if (!return_if(&g, compare(&g, DXIL_FCMP_ULE, length, floating(&g, 0.0f)))) goto done;
      length = xe_dxil_alu_scalar(&g.alu, XE_DXIL_S_RSQ, length, NULL, 0);
      offsets[0] = binary(&g, DXIL_BINOP_MUL, binary(&g, DXIL_BINOP_MUL,
        xe_dxil_alu_modify(&g.alu, delta[1], 0, 1), length), radius[0]);
      offsets[1] = binary(&g, DXIL_BINOP_MUL, binary(&g, DXIL_BINOP_MUL, delta[0], length), radius[1]);
    }
  }
  if (o->type == 2) {
    for (i = 0; i < 3; ++i) {
      a = binary(&g, DXIL_BINOP_SUB, g.position[(2 + i) % 3][0], g.position[(1 + i) % 3][0]);
      b = binary(&g, DXIL_BINOP_SUB, g.position[(2 + i) % 3][1], g.position[(1 + i) % 3][1]);
      edge[i] = binary(&g, DXIL_BINOP_ADD, binary(&g, DXIL_BINOP_MUL, a, a), binary(&g, DXIL_BINOP_MUL, b, b));
    }
    condition = binary(&g, DXIL_BINOP_AND, compare(&g, DXIL_FCMP_OGT, edge[0], edge[1]),
      compare(&g, DXIL_FCMP_OGT, edge[0], edge[2]));
    indices[0] = select_value(&g, condition, integer(&g, 0), select_value(&g,
      compare(&g, DXIL_FCMP_OGT, edge[1], edge[2]), integer(&g, 1), integer(&g, 2)));
    for (i = 1; i < 3; ++i) {
      a = binary(&g, DXIL_BINOP_ADD, indices[0], integer(&g, i));
      indices[i] = select_value(&g, compare(&g, DXIL_ICMP_ULT, a, integer(&g, 3)), a,
        binary(&g, DXIL_BINOP_SUB, a, integer(&g, 3)));
    }
  }
  for (corner = 0; corner < 4; ++corner) {
    source = o->type == 1 ? 0 : o->type == 3 ? corner ^ (corner >> 1) : corner >> 1;
    for (i = 0; i < o->interpolators; ++i) for (j = 0; j < 4; ++j) {
      value = o->type == 2 ? rectangle_value(&g, corner, indices, g.interpolator[0][i][j],
        g.interpolator[1][i][j], g.interpolator[2][i][j]) : g.interpolator[source][i][j];
      if (!output(&g, g.output_interpolator[i], j, value)) goto done;
    }
    for (i = 0; i < 4; ++i) position[i] = o->type == 2 ?
      rectangle_value(&g, corner, indices, g.position[0][i], g.position[1][i], g.position[2][i]) : g.position[source][i];
    if (o->type == 1 || o->type == 4) for (i = 0; i < 2; ++i) {
      value = o->type == 1 ? radius[i] : binary(&g, DXIL_BINOP_MUL, offsets[i], position[3]);
      k = o->type == 1 ? i ? corner & 1 : corner >> 1 : corner & 1;
      position[i] = binary(&g, k ? DXIL_BINOP_ADD : DXIL_BINOP_SUB, position[i], value);
    }
    for (i = 0; i < 4; ++i) if (!output(&g, g.output_position, i, position[i])) goto done;
    for (i = 0; i < o->clip_planes; ++i) {
      value = o->type == 2 ? rectangle_value(&g, corner, indices, g.clip[0][i], g.clip[1][i], g.clip[2][i]) : g.clip[source][i];
      if (!output(&g, g.output_clip[i / 4], i & 3, value)) goto done;
    }
    if (o->point_coordinates) for (i = 0; i < 2; ++i) {
      value = floating(&g, o->type == 1 ? (float)(i ? corner & 1 : corner >> 1) : 0.0f);
      if (!output(&g, g.output_coordinates, i, value)) goto done;
    }
    if (!stream(&g, 0)) goto done;
  }
  if (g.alu.failed || !stream(&g, 1) || !dxil_emit_ret_void(g.alu.module)) goto done;
  result = xe_dxil_shader_finish(g.shader, binary_out);
done:
  xe_dxil_shader_destroy(g.shader);
  return result;
}
