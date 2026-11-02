/* Native DXIL buffer access. MSVC C89; BSD license, see LICENSE. */
#ifndef XE_GPU_DXIL_GUEST_RESOURCES_H
#define XE_GPU_DXIL_GUEST_RESOURCES_H
#include "guest_vertex.h"
#ifdef __cplusplus
extern "C" {
#endif

enum XeDxilResourceClass {
  XE_DXIL_RESOURCE_SRV = 0, XE_DXIL_RESOURCE_UAV = 1,
  XE_DXIL_RESOURCE_CBV = 2, XE_DXIL_RESOURCE_SAMPLER = 3
};

/* Resource IDs are indices within the corresponding dx.resources class.
 * binding is the absolute register index within the declared range. The stage
 * assembler declares these resources and their register bindings. */
XeDxilValue xe_dxil_resource_handle(XeDxilAlu *alu, unsigned resource_class, unsigned id, unsigned binding);
/* Raw scalar i32 load from a declared raw buffer, address in bytes. */
XeDxilValue xe_dxil_raw_buffer_load(XeDxilAlu *alu, XeDxilValue handle, XeDxilValue byte_address);
int xe_dxil_raw_buffer_store(XeDxilAlu *alu, XeDxilValue handle,
  XeDxilValue byte_address, XeDxilValue value);
/* Dynamic dword indexing into a declared constant buffer. Returns raw i32.
 * Callers must keep word_index inside the declared constant-buffer size. */
XeDxilValue xe_dxil_cbuffer_word(XeDxilAlu *alu, XeDxilValue handle, XeDxilValue word_index);
/* A false condition branches around the memory instruction and returns zero.
 * Updates io->current_block. The raw address must be valid when condition is true. */
XeDxilValue xe_dxil_guarded_raw_load(XeDxilAlu *alu, const XeDxilAluIo *io,
                                     XeDxilValue handle, XeDxilValue byte_address,
                                     XeDxilValue condition);

typedef struct XeDxilVertexResources {
  XeDxilAlu *alu;
  const XeDxilAluIo *io;
  XeDxilValue fetch_constants, shared_memory;
} XeDxilVertexResources;

/* Direct adapters for XeDxilVertexFetch. fetch_constants contains 192 dwords
 * (96 two-dword vertex constants). shared_memory is a raw SRV or UAV handle.
 * Physical addresses outside the Xbox 512 MiB window never reach the buffer. */
XeDxilValue xe_dxil_vertex_resource_constant(void *context, unsigned index, unsigned word);
XeDxilValue xe_dxil_vertex_resource_load(void *context, XeDxilValue address, XeDxilValue in_bounds);

#ifdef __cplusplus
}
#endif
#endif
