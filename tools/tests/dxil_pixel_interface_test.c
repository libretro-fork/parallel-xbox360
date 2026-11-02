/* Native pixel interface validation fixtures. MSVC C89; BSD license. */
#include "shader_module.h"
#include <dxil_module.h>
#include <dxil_function.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
  XeDxilShaderModule *shader;
  XeDxilShaderBinary binary;
  struct dxil_module *m;
  const struct dxil_func *function;
  const struct dxil_value *args[5], *value;
  unsigned mode, type, kind, system, columns, i;
  int id, result = 1;
  FILE *file;
  if (argc != 3) return 2;
  mode = (unsigned)strtoul(argv[2], NULL, 10);
  if (mode > 4) return 2;
  shader = xe_dxil_shader_create(DXIL_PIXEL_SHADER);
  if (!shader) return 1;
  memset(&binary, 0, sizeof(binary)); m = xe_dxil_shader_module(shader);
  type = mode == 0 || mode == 2 ? DXIL_COMP_TYPE_F32 : DXIL_COMP_TYPE_U32;
  kind = mode == 2 ? DXIL_SEM_DEPTH : mode == 3 ? DXIL_SEM_STENCIL_REF : DXIL_SEM_TARGET;
  system = mode == 2 ? DXIL_PROG_SEM_DEPTH : mode == 3 ? DXIL_PROG_SEM_STENCIL_REF : DXIL_PROG_SEM_TARGET;
  columns = mode == 0 ? 4 : 1;
  id = xe_dxil_shader_signature_typed(shader, 1, mode == 2 ? "SV_Depth" : mode == 3 ? "SV_StencilRef" : "SV_Target",
    0, kind, system, mode == 2 || mode == 3 ? UINT32_MAX : 0, 0, columns, 0, type);
  if (id < 0) goto done;
  value = type == DXIL_COMP_TYPE_F32 ? dxil_module_get_float_const(m, .5f) : dxil_module_get_int32_const(m, 37);
  if (mode == 4) {
    if (!xe_dxil_shader_sample_frequency(shader)) goto done;
    function = dxil_get_function(m, "dx.op.sampleIndex", DXIL_I32);
    args[0] = dxil_module_get_int32_const(m, 90);
    value = function ? dxil_emit_call(m, function, args, 1) : NULL;
  }
  function = dxil_get_function(m, "dx.op.storeOutput", type == DXIL_COMP_TYPE_F32 ? DXIL_F32 : DXIL_I32);
  args[0] = dxil_module_get_int32_const(m, 5); args[1] = dxil_module_get_int32_const(m, id);
  args[2] = dxil_module_get_int32_const(m, 0); args[4] = value;
  for (i = 0; i < columns; ++i) {
    args[3] = dxil_module_get_int8_const(m, (int8_t)i);
    if (!function || !value || !dxil_emit_call_void(m, function, args, 5)) goto done;
  }
  if (!dxil_emit_ret_void(m) || !xe_dxil_shader_finish(shader, &binary)) goto done;
  file = fopen(argv[1], "wb");
  if (!file) goto done;
  result = fwrite(binary.data, 1, binary.size, file) != binary.size;
  if (fclose(file)) result = 1;
done:
  xe_dxil_shader_binary_free(&binary); xe_dxil_shader_destroy(shader);
  return result;
}
