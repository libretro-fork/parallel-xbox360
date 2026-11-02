/* Actual CBV/SRV vertex-fetch execution fixture. MSVC C89; BSD license. */
#include "guest_resources.h"
#include "guest_flow.h"
#include "dxil_test_storage.h"
#include <dxil_module.h>
#include <dxil_function.h>

static int write_fixture(const char *path, const char *suffix, const void *data, size_t size) {
  size_t length = strlen(path), extra = strlen(suffix) + 1;
  char *name;
  FILE *file;
  int result;
  if (length > (size_t)-1 - extra) return 0;
  name = (char *)malloc(length + extra);
  if (!name) return 0;
  memcpy(name, path, length); memcpy(name + length, suffix, extra);
  file = fopen(name, "wb"); free(name);
  if (!file) return 0;
  result = fwrite(data, 1, size, file) == size;
  if (fclose(file)) result = 0;
  return result;
}

typedef struct ResourceFixture {
  XeDxilVertexResources resources;
  XeDxilVertexFetch fetch;
} ResourceFixture;

static int fetch(void *context, const uint32_t *words, unsigned serialized,
                   XeDxilAlu *alu, XeDxilAluIo *io) {
  ResourceFixture *fixture = (ResourceFixture *)context;
  XeDxilVertexInstruction instruction;
  (void)serialized;
  return xe_dxil_decode_vertex(words, 3, &instruction) &&
    xe_dxil_emit_vertex_fetch(alu, &instruction, io, &fixture->fetch);
}

int xe_dxil_resources_test(struct dxil_module *m, const char *path) {
  static const uint32_t program[] = {
    0x52001, 0x2000, 0,
    (2u << 12) | (1u << 19) | (31u << 20) | (2u << 25),
    (33u << 16) | (1u << 13), 2 | (1u << 8),
    (3u << 12), (33u << 16) | (1u << 13) | (1u << 30), (2u << 8)
  };
  static const uint32_t addresses[] = {0,15,16,UINT32_C(0xffffffff),UINT32_C(0x08000000),UINT32_C(0x40000000)};
  uint32_t memory[32], constants[192], expected[8];
  ResourceFixture fixture;
  InstructionStorage storage;
  XeDxilAlu alu;
  XeDxilAluIo io;
  XeDxilGuestFlow flow;
  XeDxilValue handle, args[2], thread, values[4], index, value, condition;
  const struct dxil_func *function;
  const struct dxil_type *type;
  unsigned i, j, block = 0, record = 0;
  size_t length = strlen(path);
  char *name;
  FILE *golden;
  int result = 0;
  for (i = 0; i < 32; ++i) memory[i] = i;
  memory[0] = 123;
  for (i = 0; i < 192; ++i) constants[i] = i * 3 + 1;
  constants[190] = UINT32_C(0x80000010); constants[191] = 12 * 4;
  if (!write_fixture(path, ".memory", memory, sizeof(memory)) ||
      !write_fixture(path, ".constants", constants, sizeof(constants))) return 0;
  memset(&fixture, 0, sizeof(fixture)); memset(&storage, 0, sizeof(storage));
  memset(&io, 0, sizeof(io)); memset(&flow, 0, sizeof(flow));
  xe_dxil_alu_init(&alu, m);
  storage.module = m; storage.current_block = &block;
  type = dxil_module_get_array_type(m, dxil_module_get_float_type(m, 32), 256);
  storage.registers = dxil_emit_alloca(m, type, dxil_module_get_int32_const(m, 1), 4);
  if (!storage.registers) return 0;
  for (j = 0; j < 4; ++j) values[j] = dxil_module_get_float_const(m, 1.75f);
  if (!xe_dxil_test_write(&storage, 0, 0, 15, values, NULL)) return 0;
  io.context = &storage; io.read = xe_dxil_test_read; io.write = xe_dxil_test_write;
  io.current_block = &block;
  handle = xe_dxil_resource_handle(&alu, XE_DXIL_RESOURCE_UAV, 0, 0);
  fixture.resources.alu = &alu; fixture.resources.io = &io;
  fixture.resources.shared_memory = xe_dxil_resource_handle(&alu, XE_DXIL_RESOURCE_SRV, 0, 0);
  fixture.resources.fetch_constants = xe_dxil_resource_handle(&alu, XE_DXIL_RESOURCE_CBV, 0, 4);
  fixture.fetch.context = &fixture.resources;
  fixture.fetch.constant = xe_dxil_vertex_resource_constant;
  fixture.fetch.load = xe_dxil_vertex_resource_load;
  if (!handle || !fixture.resources.shared_memory || !fixture.resources.fetch_constants ||
      !xe_dxil_vertex_fetch_init(&alu, &fixture.fetch)) return 0;
  flow.module = m; flow.alu = &alu; flow.io = &io; flow.context = &fixture;
  flow.words = program; flow.word_count = ARRAY_SIZE(program); flow.control_flow_count = 1;
  flow.fetch = fetch;
  if (!xe_dxil_emit_guest_flow(&flow) || length > (size_t)-1 - 10) return 0;
  name = (char *)malloc(length + 10);
  if (!name) return 0;
  memcpy(name, path, length); memcpy(name + length, ".expected", 10);
  golden = fopen(name, "wb"); free(name);
  if (!golden) return 0;
  for (i = 0; i < 2; ++i, ++record) {
    for (j = 0; j < 4; ++j) {
      values[j] = xe_dxil_test_read(&storage, 0, 2 + i, j, NULL);
      expected[j] = xe_dxil_test_float_bits((float)(7 + i));
    }
    expected[4] = 0; expected[5] = 0; expected[6] = 0; expected[7] = UINT32_C(0x90000000) | i;
    if (!xe_dxil_test_record(m, handle, record, &alu, values, expected, golden)) goto done;
  }
  args[0] = dxil_module_get_int32_const(m, 93); args[1] = dxil_module_get_int32_const(m, 0);
  function = dxil_get_function(m, "dx.op.threadId", DXIL_I32);
  if (!function) goto done;
  thread = dxil_emit_call(m, function, args, 2);
  if (!thread) goto done;
  for (i = 0; i < 8; ++i, ++record) {
    index = dxil_emit_binop(m, DXIL_BINOP_ADD, thread, dxil_module_get_int32_const(m, (int32_t)i), 0);
    value = xe_dxil_cbuffer_word(&alu, fixture.resources.fetch_constants, index);
    if (!value) goto done;
    values[0] = dxil_emit_cast(m, DXIL_CAST_UITOFP, dxil_module_get_float_type(m, 32), value);
    for (j = 0; j < 4; ++j) { values[j] = values[0]; expected[j] = xe_dxil_test_float_bits((float)(i * 3 + 1)); }
    expected[7] = UINT32_C(0x91000000) | i;
    if (!xe_dxil_test_record(m, handle, record, &alu, values, expected, golden)) goto done;
  }
  for (i = 0; i < ARRAY_SIZE(addresses); ++i, ++record) {
    index = dxil_emit_binop(m, DXIL_BINOP_XOR, thread, dxil_module_get_int32_const(m, (int32_t)addresses[i]), 0);
    /* Last two cases deliberately claim in-bounds; the physical-window guard must
     * still prevent an outside-window or wrapped byte address from reading data. */
    condition = i >= 4 ? dxil_module_get_int1_const(m, 1) :
      dxil_emit_cmp(m, DXIL_ICMP_ULT, index, dxil_module_get_int32_const(m, 16));
    value = xe_dxil_vertex_resource_load(&fixture.resources, index, condition);
    if (!value) goto done;
    values[0] = dxil_emit_cast(m, DXIL_CAST_UITOFP, dxil_module_get_float_type(m, 32), value);
    for (j = 0; j < 4; ++j) { values[j] = values[0]; expected[j] = xe_dxil_test_float_bits(i == 0 ? 123.0f : i == 1 ? 15.0f : 0.0f); }
    expected[7] = UINT32_C(0x92000000) | i;
    if (!xe_dxil_test_record(m, handle, record, &alu, values, expected, golden)) goto done;
  }
  /* Reject malformed adapter inputs without emitting invalid DXIL. */
  xe_dxil_alu_init(&alu, m);
  value = dxil_module_get_float_const(m, 0.0f);
  if (xe_dxil_cbuffer_word(&alu, fixture.resources.fetch_constants, value) || !alu.failed) goto done;
  xe_dxil_alu_init(&alu, m);
  if (xe_dxil_raw_buffer_load(&alu, fixture.resources.shared_memory, value) || !alu.failed) goto done;
  xe_dxil_alu_init(&alu, m);
  if (xe_dxil_resource_handle(&alu, 4, 0, 0) || !alu.failed) goto done;
  xe_dxil_alu_init(&alu, m);
  if (xe_dxil_vertex_resource_constant(&fixture.resources, 96, 0) || !alu.failed) goto done;
  xe_dxil_alu_init(&alu, m);
  if (xe_dxil_guarded_raw_load(&alu, &io, fixture.resources.shared_memory,
      dxil_module_get_int32_const(m, 0), dxil_module_get_int32_const(m, 1)) || !alu.failed) goto done;
  result = dxil_emit_ret_void(m);
done:
  if (fclose(golden)) result = 0;
  return result;
}
