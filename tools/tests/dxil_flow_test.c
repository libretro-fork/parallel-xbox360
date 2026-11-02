/* Whole-microcode control-flow fixtures. MSVC C89; BSD license. */
#include "guest_flow.h"
#include "dxil_test_storage.h"
#include <dxil_module.h>
#include <dxil_function.h>

#define CF_PAIR(al,ah,bl,bh) ((uint32_t)(al)), \
  (((uint32_t)(ah) & 65535) | ((uint32_t)(bl) << 16)), \
  (((uint32_t)(bl) >> 16) | ((uint32_t)(bh) << 16))

/* Loop aL=2,3,4. Calls add one to r1 each iteration. A true jump bypasses
 * a bad tail, then the shader exports r0. Scalar ps accumulates in the loop. */
static const uint32_t loop_call[] = {
  CF_PAIR(3,0xc400,5,0x7000),
  CF_PAIR(0x1006,0x1000,0x2008,0x9000),
  CF_PAIR(2,0x8000,7,0xb400),
  CF_PAIR(0x1007,0x2000,0x1008,0x2000),
  CF_PAIR(0x1009,0x1000,0,0xa000),
  CF_PAIR(0,0,0,0),
  0x04010000,0x80000000,0x80000000,
  0xc8010000,0,0xa000ff00,
  0xc8018000,0,0xe2000000,
  0xc8010001,0x00000100,0xa0010000
};

/* RETURN without a call is a no-op. False conditional end/call must fall
 * through. Calls with multiple return sites include a nested subroutine. */
static const uint32_t nested_calls[] = {
  CF_PAIR(0,0xa000,0x1006,0x4404),
  CF_PAIR(8,0x9404,0x2008,0x9000),
  CF_PAIR(0x200a,0x9000,0x1007,0x2000),
  CF_PAIR(0,0,0,0),
  CF_PAIR(0x1008,0x1000,0,0xa000),
  CF_PAIR(0x2008,0x9000,0,0xa000),
  0xc8010000,0,0xa000ff00,
  0xc8018000,0,0xe2000000,
  0xc8010000,0x00000100,0xa0000000
};

/* The repeat loop inherits the outer aL. A predicated inner break leaves
 * one iteration; signed -1 increments drive outer aL=5,4,3. The final pair
 * checks true/false predicate conditions on conditional shader termination. */
static const uint32_t repeat_break[] = {
  CF_PAIR(7,0x7000,0x12004,0x7000),
  CF_PAIR(0x1006,0x1000,0x210002,0x8400),
  CF_PAIR(0x1007,0x1000,1,0x8000),
  CF_PAIR(0,0,0x1008,0x6400),
  CF_PAIR(0x1009,0x6000,0,0),
  CF_PAIR(0,0,0,0),
  0x74010000,0x80000000,0x80000000,
  0x6c010001,0x80000000,0x80010000,
  0xc8010000,0,0xa000ff00,
  0xc8018000,0,0xe2000000
};

/* Zero-count loops skip their body. Fetch and allocation hooks are
 * instrumented here; this does not implement vertex/texture fetch lowering. */
static const uint32_t zero_fetch[] = {
  CF_PAIR(3,0x7000,0x1004,0x1000),
  CF_PAIR(1,0x8000,0x31005,0x1000),
  CF_PAIR(2,0xc600,0x1006,0x4400),
  CF_PAIR(0x1004,0x2000,0,0),
  0xc8010000,0,0xa000ff00,
  0x12345678,0x9abcdef0,0,
  0xc8018000,0,0xe2050500
};

static const uint32_t predicate_calls[] = {
  CF_PAIR(0x1008,0x1000,0x400c,0x9400),
  CF_PAIR(0x400f,0xb000,12,0x9400),
  CF_PAIR(7,0x7000,0x100a,0x1000),
  CF_PAIR(0x200005,0x8000,0x100d,0x1000),
  CF_PAIR(0x400c,0x9400,0x400b,0xb000),
  CF_PAIR(0x100c,0x2000,0x100b,0x2000),
  CF_PAIR(0x1009,0x1000,0,0xa000),
  CF_PAIR(0,0,0x100c,0x2000),
  0x6c000000,0,0xe2000004,
  0xc8010001,0x00000100,0xa0010000,
  0xc8010000,0x00000100,0xa0000000,
  0xc8018000,0,0xe2000000,
  0xc8010000,0,0xa000ff00,
  0x74000000,0,0xe2000000
};

static const uint32_t bad_jump[] = {CF_PAIR(0x3fff,0xb000,0,0x2000)};
static const uint32_t bad_exec[] = {CF_PAIR(0x1fff,0x2000,0,0)};

typedef struct Program {
  const uint32_t *words;
  size_t word_count;
  unsigned cf_count;
  uint32_t loop[2];
  float registers[8];
  float exported;
  float loop_address;
  float previous_scalar;
} Program;

static const Program programs[] = {
  {loop_call,ARRAY_SIZE(loop_call),12,{0x010203,0},{36,3,0,0,35,0,0,0},36,0,9},
  {nested_calls,ARRAY_SIZE(nested_calls),12,{0,0},{2,0,0,0,0,0,0,0},2,0,0},
  {repeat_break,ARRAY_SIZE(repeat_break),12,{0xff0503,0x026302},{48,48,0,0,0,0,0,0},48,0,1},
  {zero_fetch,ARRAY_SIZE(zero_fetch),8,{0x010700,0},{0,0,0,0,50,2,0,0},2,7,0},
  {predicate_calls,ARRAY_SIZE(predicate_calls),16,{0x010003,0},{3,2,0,0,0,0,0,0},3,0,1}
};

typedef struct Fixture {
  InstructionStorage storage;
  const Program *program;
  XeDxilValue thread_id;
  unsigned current_block;
} Fixture;

static XeDxilValue boolean_constant(void *context, unsigned index) {
  Fixture *fixture = (Fixture *)context;
  return dxil_emit_cmp(fixture->storage.module, index & 1 ? DXIL_ICMP_NE : DXIL_ICMP_EQ,
    fixture->thread_id, dxil_module_get_int32_const(fixture->storage.module, 0));
}

static XeDxilValue loop_constant(void *context, unsigned index) {
  Fixture *fixture = (Fixture *)context;
  struct dxil_module *m = fixture->storage.module;
  XeDxilValue extra;
  XeDxilValue packed;
  if (index >= 2) return NULL;
  packed = dxil_module_get_int32_const(m, (int32_t)fixture->program->loop[index]);
  if (index) return packed;
  extra = dxil_emit_binop(m, DXIL_BINOP_AND, fixture->thread_id, dxil_module_get_int32_const(m, 1), 0);
  return extra ? dxil_emit_binop(m, DXIL_BINOP_ADD, packed, extra, 0) : NULL;
}

static int increment_register(Fixture *fixture, unsigned index, float delta) {
  struct dxil_module *m = fixture->storage.module;
  XeDxilValue values[4];
  values[0] = xe_dxil_test_read(&fixture->storage, 0, index, 0, NULL);
  if (!values[0]) return 0;
  values[0] = dxil_emit_binop(m, DXIL_BINOP_ADD, values[0], dxil_module_get_float_const(m, delta), 0);
  return values[0] && xe_dxil_test_write(&fixture->storage, 0, index, 1, values, NULL);
}

static int fetch(void *context, const uint32_t *words, unsigned serialized,
                   XeDxilAlu *alu, XeDxilAluIo *io) {
  Fixture *fixture = (Fixture *)context;
  (void)alu; (void)io;
  if (words[0] != UINT32_C(0x12345678) || words[1] != UINT32_C(0x9abcdef0)) return 0;
  return increment_register(fixture, 5, serialized ? 2.0f : 1.0f);
}

static int allocation(void *context, unsigned type, unsigned size, unsigned cf_index,
                       XeDxilAlu *alu, XeDxilAluIo *io) {
  Fixture *fixture = (Fixture *)context;
  (void)cf_index; (void)alu; (void)io;
  return increment_register(fixture, 4, (float)(type * 16 + size));
}

int xe_dxil_flow_test(struct dxil_module *m, const char *path, unsigned index) {
  Fixture fixture;
  XeDxilGuestFlow flow;
  XeDxilAluIo io;
  XeDxilAlu alu;
  const struct dxil_func *function;
  const struct dxil_type *array;
  XeDxilValue args[5];
  XeDxilValue values[4];
  XeDxilValue handle;
  uint32_t expected[8];
  char *expected_path;
  FILE *golden;
  unsigned i;
  unsigned j;
  unsigned first = 123;
  int result = 0;
  size_t old_blocks = m->cur_emitting_func->num_basic_block_ids;
  size_t length = strlen(path);
  if (index > 10 || length > (size_t)-1 - 10) return 0;
  if (dxil_function_reserve_blocks(m, 0, &first) || first != 123 ||
      dxil_function_reserve_blocks(m, UINT_MAX, &first) || first != 123 ||
      m->cur_emitting_func->num_basic_block_ids != old_blocks) return 0;
  memset(&fixture, 0, sizeof(fixture));
  fixture.storage.module = m;
  fixture.storage.current_block = &fixture.current_block;
  fixture.program = &programs[index < ARRAY_SIZE(programs) ? index : 3];
  array = dxil_module_get_array_type(m, dxil_module_get_float_type(m, 32), 256);
  fixture.storage.registers = dxil_emit_alloca(m, array, dxil_module_get_int32_const(m, 1), 4);
  fixture.storage.exports = dxil_emit_alloca(m, array, dxil_module_get_int32_const(m, 1), 4);
  if (!fixture.storage.registers || !fixture.storage.exports) return 0;
  for (j = 0; j < 4; ++j) values[j] = dxil_module_get_float_const(m, 0.0f);
  for (i = 0; i < 8; ++i) if (!xe_dxil_test_write(&fixture.storage, 0, i, 15, values, NULL)) return 0;
  if (!xe_dxil_test_write(&fixture.storage, 1, 0, 15, values, NULL)) return 0;
  args[0] = dxil_module_get_int32_const(m, 93); args[1] = dxil_module_get_int32_const(m, 0);
  function = dxil_get_function(m, "dx.op.threadId", DXIL_I32);
  if (!function) return 0;
  fixture.thread_id = dxil_emit_call(m, function, args, 2);
  if (!fixture.thread_id) return 0;
  xe_dxil_alu_init(&alu, m);
  memset(&io, 0, sizeof(io));
  io.context = &fixture.storage; io.read = xe_dxil_test_read; io.write = xe_dxil_test_write;
  io.current_block = &fixture.current_block;
  memset(&flow, 0, sizeof(flow));
  flow.module = m; flow.alu = &alu; flow.io = &io; flow.context = &fixture;
  flow.words = fixture.program->words; flow.word_count = fixture.program->word_count;
  flow.control_flow_count = fixture.program->cf_count;
  flow.boolean_constant = boolean_constant; flow.loop_constant = loop_constant;
  flow.fetch = fetch; flow.allocation = allocation;
  if (index == 5) { flow.words = bad_jump; flow.word_count = ARRAY_SIZE(bad_jump); flow.control_flow_count = 2; }
  if (index == 6) { flow.words = bad_exec; flow.word_count = ARRAY_SIZE(bad_exec); flow.control_flow_count = 2; }
  if (index == 7) flow.fetch = NULL;
  if (index == 8) flow.loop_constant = NULL;
  if (index == 9) flow.boolean_constant = NULL;
  if (index == 10) flow.allocation = NULL;
  if (!xe_dxil_emit_guest_flow(&flow)) return 0;
  args[0] = dxil_module_get_int32_const(m, 57);
  args[1] = dxil_module_get_int8_const(m, DXIL_RESOURCE_CLASS_UAV);
  args[2] = dxil_module_get_int32_const(m, 0); args[3] = args[2];
  args[4] = dxil_module_get_int1_const(m, 0);
  function = dxil_get_function(m, "dx.op.createHandle", DXIL_NONE);
  if (!function) return 0;
  handle = dxil_emit_call(m, function, args, 5);
  if (!handle) return 0;
  expected_path = (char *)malloc(length + 10);
  if (!expected_path) return 0;
  memcpy(expected_path, path, length); memcpy(expected_path + length, ".expected", 10);
  golden = fopen(expected_path, "wb"); free(expected_path);
  if (!golden) return 0;
  for (i = 0; i < 11; ++i) {
    for (j = 0; j < 4; ++j) {
      if (i < 8) values[j] = xe_dxil_test_read(&fixture.storage, 0, i, j, NULL);
      else if (i == 8) {
        args[0] = xe_dxil_test_pointer(&fixture.storage, fixture.storage.exports, 0, j, NULL);
        values[j] = args[0] ? dxil_emit_load(m, args[0], 4, 0) : NULL;
      } else if (i == 9) values[j] = dxil_emit_cast(m, DXIL_CAST_SITOFP,
        dxil_module_get_float_type(m, 32), io.loop_address);
      else values[j] = alu.previous_scalar;
      expected[j] = xe_dxil_test_float_bits(j && i < 9 ? 0.0f :
        (i < 8 ? fixture.program->registers[i] : (i == 8 ? fixture.program->exported :
          (i == 9 ? fixture.program->loop_address : fixture.program->previous_scalar))));
    }
    expected[4] = 0; expected[5] = 0; expected[6] = 0; expected[7] = UINT32_C(0x70000000) | i;
    if (!xe_dxil_test_record(m, handle, i, &alu, values, expected, golden)) goto done;
  }
  result = dxil_emit_ret_void(m);
done:
  if (fclose(golden)) result = 0;
  return result;
}
