/* Native shader container construction. MSVC C89; BSD license. */
#ifndef XE_GPU_DXIL_SHADER_MODULE_H
#define XE_GPU_DXIL_SHADER_MODULE_H
#include <stddef.h>
#include <stdint.h>
struct dxil_module;
typedef struct XeDxilShaderModule XeDxilShaderModule;
typedef struct XeDxilShaderBinary {
  unsigned char *data;
  size_t size;
} XeDxilShaderBinary;

#ifdef __cplusplus
extern "C" {
#endif
/* Stage values follow DXIL: pixel=0, vertex=1, geometry=2, hull=3,
 * domain=4, compute=5. All construction state is owned by the returned arena. */
XeDxilShaderModule *xe_dxil_shader_create(unsigned stage);
struct dxil_module *xe_dxil_shader_module(XeDxilShaderModule *shader);
void xe_dxil_shader_destroy(XeDxilShaderModule *shader);
/* direction: input=0, output=1, patch=2. IDs are per direction, row packing
 * is explicit, and semantic_kind/system_value use the DXIL enum values. */
int xe_dxil_shader_signature(XeDxilShaderModule *shader, unsigned direction,
                               const char *name, unsigned semantic_index,
                               unsigned semantic_kind, unsigned system_value,
                               unsigned row, unsigned column, unsigned columns, unsigned interpolation);
/* Resource class: SRV=0, UAV=1, CBV=2, sampler=3. Kind values follow DXIL.
 * Returns a class-local resource ID, or -1. count must be nonzero; UINT32_MAX
 * declares an unbounded array. CBVs additionally specify their byte size. */
int xe_dxil_shader_resource(XeDxilShaderModule *shader, unsigned resource_class,
                              unsigned kind, unsigned space, unsigned binding,
                              unsigned count, unsigned byte_size);
int xe_dxil_shader_resource_typed(XeDxilShaderModule *shader, unsigned resource_class,
                                    unsigned kind, unsigned space, unsigned binding,
                                    unsigned count, unsigned byte_size, unsigned component_type);
int xe_dxil_shader_geometry(XeDxilShaderModule *shader, unsigned input_primitive,
                              unsigned max_vertices, unsigned output_topology);
int xe_dxil_shader_threads(XeDxilShaderModule *shader, unsigned x, unsigned y, unsigned z);
/* The body must end in return. Successful finish transfers the serialized
 * container buffer directly to binary; no intermediary output copy. */
int xe_dxil_shader_finish(XeDxilShaderModule *shader, XeDxilShaderBinary *binary);
void xe_dxil_shader_binary_free(XeDxilShaderBinary *binary);
#ifdef __cplusplus
}
#endif
#endif
