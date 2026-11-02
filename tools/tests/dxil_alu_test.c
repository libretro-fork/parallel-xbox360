/* Xbox ALU golden cases for direct DXIL lowering. MSVC C89; BSD license. */
#include "guest_alu.h"
#include "dxil_test_storage.h"
#include "guest_alu_instruction.h"
#include <dxil_module.h>
#include <dxil_function.h>
#include <stdio.h>
#include <float.h>

typedef struct VectorCase {
  unsigned opcode;
  float result[4];
  unsigned predicate;
  int address;
  unsigned killed;
} VectorCase;

static const VectorCase vector_cases[] = {
  {0, {7,-1,-1,2},0,0,0}, {1, {10,-6,0,-8},0,0,0},
  {2, {5,2,0,4},0,0,0}, {3, {2,-3,-1,-2},0,0,0},
  {4, {0,0,0,0},0,0,0}, {5, {0,0,1,1},0,0,0},
  {6, {0,0,1,1},0,0,0}, {7, {1,1,1,1},0,0,0},
  {8, {0,0,0,0},0,0,0}, {9, {2,-3,0,4},0,0,0},
  {10,{2,-3,0,4},0,0,0}, {11,{17,2,9,2},0,0,0},
  {12,{7,8,-1,10},0,0,0}, {13,{5,8,-1,-2},0,0,0},
  {14,{5,8,9,-2},0,0,0}, {15,{-4,-4,-4,-4},0,0,0},
  {16,{4,4,4,4},0,0,0}, {17,{11,11,11,11},0,0,0},
  {18,{2,0,8,2},0,0,0}, {19,{4,4,4,4},0,0,0},
  {20,{3,3,3,3},0,0,0}, {21,{3,3,3,3},0,0,0},
  {22,{3,3,3,3},0,0,0}, {23,{3,3,3,3},0,0,0},
  {24,{0,0,0,0},0,0,0}, {25,{1,1,1,1},0,0,1},
  {26,{1,1,1,1},0,0,1}, {27,{1,1,1,1},0,0,1},
  {28,{1,-6,0,-2},0,0,0}, {29,{5,2,0,4},0,4,0}
};

typedef struct ScalarCase {
  unsigned opcode;
  float result;
  unsigned predicate;
  int address;
  unsigned killed;
} ScalarCase;

static const ScalarCase scalar_cases[] = {
  {0,1,0,0,0}, {1,6,0,0,0}, {2,-12,0,0,0}, {3,8,0,0,0},
  {4,-FLT_MAX,0,0,0}, {5,4,0,0,0}, {6,-3,0,0,0},
  {7,0,0,0,0}, {8,1,0,0,0}, {9,1,0,0,0}, {10,1,0,0,0},
  {11,0,0,0,0}, {12,4,0,0,0}, {13,4,0,0,0}, {14,16,0,0,0},
  {15,2,0,0,0}, {16,2,0,0,0}, {17,0.25f,0,0,0}, {18,0.25f,0,0,0},
  {19,0.25f,0,0,0}, {20,0.5f,0,0,0}, {21,0.5f,0,0,0}, {22,0.5f,0,0,0},
  {23,4,0,4,0}, {24,4,0,4,0}, {25,7,0,0,0}, {26,2,0,0,0},
  {27,1,0,0,0}, {28,0,1,0,0}, {29,0,1,0,0}, {30,0,1,0,0},
  {31,4,0,0,0}, {32,3,0,0,0}, {33,FLT_MAX,0,0,0}, {34,4,0,0,0},
  {35,0,0,0,0}, {36,1,0,0,1}, {37,1,0,0,1}, {38,1,0,0,1},
  {39,0,0,0,0}, {40,2,0,0,0},
  {42,-12,0,0,0}, {43,-12,0,0,0}, {44,1,0,0,0}, {45,1,0,0,0},
  {46,7,0,0,0}, {47,7,0,0,0}, {48,0,0,0,0}, {49,1,0,0,0}, {50,2,0,0,0}
};

typedef struct ExtraVectorCase {
  unsigned opcode;
  float inputs[3][4];
  float result[4];
  unsigned predicate;
  int address;
  unsigned killed;
} ExtraVectorCase;

static const ExtraVectorCase extra_vectors[] = {
  {18,{{0,0,1,0},{0},{0}}, {-0.0f,-0.0f,2,0},0,0,0},
  {18,{{0,0,-1,0},{0},{0}}, {-0.0f,0,-2,1},0,0,0},
  {18,{{0,0,0,1},{0},{0}}, {0,0,2,2},0,0,0},
  {18,{{0,0,0,-1},{0},{0}}, {-0.0f,0,-2,3},0,0,0},
  {18,{{1,1,0,0},{0},{0}}, {-0.0f,0,2,4},0,0,0},
  {18,{{-1,-1,0,0},{0},{0}}, {-0.0f,-0.0f,-2,5},0,0,0},
  {18,{{1,1,1,1},{0},{0}}, {-1,1,2,4},0,0,0},
  {18,{{0,0,1,1},{0},{0}}, {0,1,2,2},0,0,0},
  {11,{{4097,4097,4097,4097},{4097,4097,4097,4097},
       {-16785408,-16785408,-16785408,-16785408}}, {0,0,0,0},0,0,0},
  {16,{{16777216,1,-16777216,0},{1,1,1,1},{0}}, {0,0,0,0},0,0,0},
  {20,{{0},{0},{0}}, {0,0,0,0},1,0,0},
  {21,{{0},{1,0,0,1},{0}}, {0,0,0,0},1,0,0},
  {22,{{0},{1,0,0,1},{0}}, {0,0,0,0},1,0,0},
  {23,{{0},{0},{0}}, {0,0,0,0},1,0,0},
  {20,{{2,0,0,0},{0},{0}}, {3,3,3,3},1,0,0},
  {29,{{0,0,0,-300},{0},{0}}, {0,0,0,0},0,-256,0}
};

typedef struct BitVectorCase {
  unsigned opcode;
  uint32_t inputs[3][4];
  uint32_t result[4];
  unsigned killed;
} BitVectorCase;

static const BitVectorCase bit_vectors[] = {
  {2, {{0x7fc12345,0x3f800000,0x80000000,0},
       {0x3f800000,0x7fc12345,0,0x80000000},{0}},
      {0x3f800000,0x7fc00000,0x80000000,0},0},
  {3, {{0x7fc12345,0x3f800000,0x80000000,0},
       {0x3f800000,0x7fc12345,0,0x80000000},{0}},
      {0x3f800000,0x7fc00000,0,0x80000000},0},
  {1, {{0x7fc12345,0,0x80000000,1},
       {0,0x7f800000,0x7fc12345,0x7f800000},{0}}, {0,0,0,0},0},
  {11,{{0},{0x7fc12345,0x7fc12345,0x7fc12345,0x7fc12345},
       {0x80000000,0x40000000,0xbf800000,0x7fc12345}},
      {0,0x40000000,0xbf800000,0x7fc00000},0},
  {27,{{0x7fc12345,0,0,0},{0},{0}},
      {0x3f800000,0x3f800000,0x3f800000,0x3f800000},1}
};

/* Boundary cases use raw IEEE bit patterns so signed zeros and NaN operands
 * survive host constant handling. Expected NaNs are checked by class. */
typedef struct BoundaryCase {
  unsigned opcode;
  uint32_t a;
  uint32_t b;
  uint32_t previous;
  uint32_t result;
  unsigned predicate;
  int address;
  unsigned killed;
  int identical;
  int round_toward_zero;
} BoundaryCase;

static const BoundaryCase boundaries[] = {
  {2,0,0x7fc12345,0,0,0,0,0,0,0},
  {2,0x80000000,0x7f800000,0,0,0,0,0,0,0},
  {2,1,0x7fc12345,0,0,0,0,0,0,0},
  {2,0x80000000,0x80000000,0,0,0,0,0,1,0},
  {5,0x7fc12345,0x3f800000,0,0x3f800000,0,0,0,0,0},
  {5,0x3f800000,0x7fc12345,0,0x7fc00000,0,0,0,0,0},
  {6,0x7fc12345,0x3f800000,0,0x3f800000,0,0,0,0,0},
  {6,0x3f800000,0x7fc12345,0,0x7fc00000,0,0,0,0,0},
  {10,0x7fc12345,0,0,0x3f800000,0,0,0,0,0},
  {11,0xbf900000,0,0,0x3f600000,0,0,0,0,0},
  {12,0xbf900000,0,0,0xbf800000,0,0,0,0,0},
  {13,0xbf900000,0,0,0xc0000000,0,0,0,0,0},
  {15,0,0,0,0xff7fffff,0,0,0,0,0},
  {17,0,0,0,0x7f7fffff,0,0,0,0,0},
  {17,0x80000000,0,0,0xff7fffff,0,0,0,0,0},
  {18,0,0,0,0,0,0,0,0,0},
  {18,0x80000000,0,0,0x80000000,0,0,0,0,0},
  {19,0,0,0,0x7f800000,0,0,0,0,0},
  {19,0x80000000,0,0,0xff800000,0,0,0,0,0},
  {19,0x40400000,0,0,0x3eaaaaac,0,0,0,0,0},
  {23,0xbf400000,0,0,0,0,0,0,0,0},
  {24,0x43a00000,0,0,0x43a00000,0,255,0,0,0},
  {27,0,0,0,0,1,0,0,0,0},
  {28,0x7fc12345,0,0,0,1,0,0,0,0},
  {31,0x3f800000,0,0,0,1,0,0,0,0},
  {32,0x3f000000,0,0,0,1,0,0,0,0},
  {34,0x80000000,0,0,0,1,0,0,0,0},
  {39,0x3f800000,0,0,0x3f800000,0,0,1,0,0},
  {4,0x40000000,0x3f800000,0x40400000,0x40c00000,0,0,0,0,0},
  {4,0x40000000,0x3f800000,0xff7fffff,0xff7fffff,0,0,0,0,0},
  {42,0x3f800fff,0x3f800fff,0,0x3f802000,0,0,0,0,0},
  {42,0x3f800fff,0x3f800fff,0,0x3f801fff,0,0,0,0,1}
};

uint32_t xe_dxil_test_float_bits(float value) {
  uint32_t result;
  memcpy(&result, &value, sizeof(result));
  return result;
}

static int store(struct dxil_module *m, XeDxilValue handle,
                   unsigned word, XeDxilValue value) {
  XeDxilValue args[9];
  const struct dxil_func *function;
  function = dxil_get_function(m, "dx.op.bufferStore", DXIL_I32);
  args[0] = dxil_module_get_int32_const(m, 69); args[1] = handle;
  args[2] = dxil_module_get_int32_const(m, (int32_t)(word * 4));
  args[3] = dxil_module_get_undef(m, dxil_module_get_int_type(m, 32));
  args[4] = value; args[5] = args[3]; args[6] = args[3]; args[7] = args[3];
  args[8] = dxil_module_get_int8_const(m, 1);
  return function && value && dxil_emit_call_void(m, function, args, 9);
}

int xe_dxil_test_record(struct dxil_module *m, XeDxilValue handle, unsigned index,
                    XeDxilAlu *alu, XeDxilValue values[4],
                    const uint32_t expected[8], FILE *golden) {
  XeDxilValue value;
  unsigned i;
  if (alu->failed) return 0;
  for (i = 0; i < 8; ++i) {
    if (i < 4) value = dxil_emit_cast(m, DXIL_CAST_BITCAST,
      dxil_module_get_int_type(m, 32), values[i]);
    else if (i == 4 || i == 6) value = dxil_emit_cast(m, DXIL_CAST_ZEXT,
      dxil_module_get_int_type(m, 32), i == 4 ? alu->predicate : alu->killed);
    else if (i == 5) value = alu->address;
    else value = dxil_module_get_int32_const(m, (int32_t)expected[i]);
    if (!store(m, handle, index * 8 + i, value)) return 0;
  }
  return fwrite(expected, sizeof(uint32_t), 8, golden) == 8;
}


XeDxilValue xe_dxil_test_pointer(InstructionStorage *storage, XeDxilValue base,
                                       unsigned index, unsigned component, XeDxilValue offset) {
  struct dxil_module *m = storage->module;
  XeDxilValue args[3];
  XeDxilValue address;
  address = dxil_module_get_int32_const(m, (int32_t)index);
  if (offset) address = dxil_emit_binop(m, DXIL_BINOP_ADD, address, offset, 0);
  address = dxil_emit_binop(m, DXIL_BINOP_MUL, address, dxil_module_get_int32_const(m, 4), 0);
  address = dxil_emit_binop(m, DXIL_BINOP_ADD, address, dxil_module_get_int32_const(m, (int32_t)component), 0);
  args[0] = base; args[1] = dxil_module_get_int32_const(m, 0); args[2] = address;
  return address ? dxil_emit_gep_inbounds(m, args, 3) : NULL;
}

XeDxilValue xe_dxil_test_read(void *context, unsigned constant, unsigned index,
                                    unsigned component, XeDxilValue offset) {
  InstructionStorage *storage = (InstructionStorage *)context;
  struct dxil_module *m = storage->module;
  XeDxilValue address;
  if (!constant) {
    address = xe_dxil_test_pointer(storage, storage->registers, index, component, offset);
    return address ? dxil_emit_load(m, address, 4, 0) : NULL;
  }
  /* Procedural constants cN = float4(4*N, 4*N+1, 4*N+2, 4*N+3), so
   * addressing and component selection have independent golden answers. */
  address = dxil_module_get_int32_const(m, (int32_t)index);
  if (offset) address = dxil_emit_binop(m, DXIL_BINOP_ADD, address, offset, 0);
  address = dxil_emit_binop(m, DXIL_BINOP_AND, address, dxil_module_get_int32_const(m, 255), 0);
  address = dxil_emit_binop(m, DXIL_BINOP_MUL, address, dxil_module_get_int32_const(m, 4), 0);
  address = dxil_emit_binop(m, DXIL_BINOP_ADD, address, dxil_module_get_int32_const(m, (int32_t)component), 0);
  return address ? dxil_emit_cast(m, DXIL_CAST_UITOFP, dxil_module_get_float_type(m, 32), address) : NULL;
}

int xe_dxil_test_write(void *context, unsigned exporting, unsigned index,
                               unsigned mask, XeDxilValue values[4], XeDxilValue offset) {
  InstructionStorage *storage = (InstructionStorage *)context;
  XeDxilValue pointer;
  unsigned i;
  for (i = 0; i < 4; ++i) if (mask & (1u << i)) {
    pointer = xe_dxil_test_pointer(storage, exporting ? storage->exports : storage->registers,
      index, i, offset);
    if (!pointer || !values[i] || !dxil_emit_store(storage->module, values[i], pointer, 4, 0)) return 0;
  }
  return 1;
}

/* Instrument the stage hook instead of discarding compute invocations. The
 * counter lets the GPU test verify that inactive predicates suppress kills. */
static int instruction_kill(void *context, XeDxilValue condition) {
  InstructionStorage *storage = (InstructionStorage *)context;
  struct dxil_module *m = storage->module;
  XeDxilValue count;
  struct dxil_func_def *function = m->cur_emitting_func;
  unsigned body = storage->next_kill_block;
  unsigned merge = body + 1;
  storage->next_kill_block += 2;
  if (merge >= function->num_basic_block_ids ||
      !dxil_emit_branch(m, condition, body, merge)) return 0;
  function->basic_block_ids[body] = (int)function->curr_block;
  *storage->current_block = body;
  count = dxil_emit_load(m, storage->kill_count, 4, 0);
  if (!count) return 0;
  count = dxil_emit_binop(m, DXIL_BINOP_ADD, count, dxil_module_get_int32_const(m, 1), 0);
  if (!count || !dxil_emit_store(m, count, storage->kill_count, 4, 0) ||
      !dxil_emit_branch(m, NULL, merge, 0)) return 0;
  function->basic_block_ids[merge] = (int)function->curr_block;
  *storage->current_block = merge;
  return 1;
}

typedef struct InstructionCase {
  uint32_t words[3];
  unsigned exporting;
  unsigned destination;
  float result[4];
  unsigned predicate;
  int address;
  unsigned killed;
  unsigned body;
} InstructionCase;

static const InstructionCase instruction_cases[] = {
  /* Co-issue: scalar reads old r0.x, not the vector's new r0.x. */
  {{0x08c30000,0,0xe0010200},0,0,{11,22,4,4},0,0,0,0},
  {{0x6c000000,0,0xe2000004},0,0,{11,22,4,4},1,0,0,0},
  {{0xc80f0000,0x18000000,0xe0010200},0,0,{11,22,33,44},1,0,0,1},
  /* Inactive MAXA/SETP_CLR must not change a0, p0 or ps. */
  {{0x84000000,0x10000000,0xfd010200},0,0,{11,22,33,44},1,0,0,3},
  {{0x04100500,0,0xe2000002},0,5,{4,0,0,0},1,0,0,0},
  {{0x6c000000,0x18000000,0xe2000001},0,0,{11,22,33,44},0,0,0,5},
  {{0xc80f0000,0x10000000,0xe0020200},0,0,{2,4,6,8},0,0,0,7},
  /* Export X=vector, Y=one, Z=scalar, W=zero. */
  {{0xc863c000,0,0xe2010100},1,0,{10,1,1,0},0,0,0,0},
  {{0x02f00600,0,0xe2000003},0,6,{0,0,0,0},0,0,0,0},
  {{0x04f00500,0,0xe2000002},0,5,{-3,-3,-3,-3},0,0,0,0},
  {{0xc90f0000,0,0xe0010200},0,0,{1,1,1,1},0,0,0,0},
  {{0xc80f0007,0x04007700,0xe0830200},0,7,{2,0,-2,-4},0,0,0,0},
  {{0xc80f0045,0,0xe0400200},0,7,{2,4,6,8},0,0,0,0},
  {{0xc80f0007,0x80000000,0x60050200},0,7,{29,31,33,35},0,0,0,0},
  {{0xc8000000,0,0xfd020200},0,7,{29,31,33,35},0,4,0,0},
  {{0xc80f0007,0xa0000000,0x60050200},0,7,{37,39,41,43},0,4,0,0},
  {{0xc80f0007,0x60000000,0x20050600},0,7,{60,62,64,66},0,4,0,0},
  /* Three-source vector op makes the paired scalar use source 3's W/Z. */
  {{0x008f0007,0,0xeb010203},0,7,{8,37,86,155},0,4,0,0},
  {{0xc8000000,0,0xe2000000},0,0,{1,1,1,-9},0,4,0,0},
  {{0xa8f00700,0x80000000,0xe2000005},0,7,{31,31,31,31},0,4,0,0},
  {{0xacf00780,0x81000000,0xe2000005},0,7,{62,62,62,62},0,4,0,0},
  {{0xc8000000,0x18000000,0xfb010200},0,7,{62,62,62,62},0,4,0,9},
  {{0xc8000000,0,0xfb010200},0,7,{62,62,62,62},0,4,1,0}
};

static int instruction_test(struct dxil_module *m, XeDxilValue handle,
                              unsigned *index, FILE *golden) {
  static const float initial[4][4] = {{1,2,3,4},{10,20,30,40},{1,2,3,4},{-2,-3,-4,-5}};
  InstructionStorage storage;
  XeDxilAluIo io;
  XeDxilAlu alu;
  XeDxilAluInstruction instruction;
  XeDxilValue values[4];
  XeDxilValue pointer;
  const struct dxil_type *array;
  uint32_t expected[8];
  uint32_t invalid[3] = {0xcc000000,0,0xe2000000};
  unsigned block = 0;
  unsigned i;
  unsigned j;
  storage.module = m;
  storage.current_block = &block; storage.next_kill_block = 20;
  array = dxil_module_get_array_type(m, dxil_module_get_float_type(m, 32), 256);
  storage.registers = dxil_emit_alloca(m, array, dxil_module_get_int32_const(m, 1), 4);
  storage.exports = dxil_emit_alloca(m, array, dxil_module_get_int32_const(m, 1), 4);
  storage.kill_count = dxil_emit_alloca(m, dxil_module_get_int_type(m, 32),
    dxil_module_get_int32_const(m, 1), 4);
  if (!storage.registers || !storage.exports || !storage.kill_count ||
      !dxil_emit_store(m, dxil_module_get_int32_const(m, 0), storage.kill_count, 4, 0)) return 0;
  for (i = 0; i < 8; ++i) {
    for (j = 0; j < 4; ++j) values[j] = dxil_module_get_float_const(m, i < 4 ? initial[i][j] : 0.0f);
    if (!xe_dxil_test_write(&storage, 0, i, 15, values, NULL)) return 0;
  }
  for (j = 0; j < 4; ++j) values[j] = dxil_module_get_float_const(m, 0.0f);
  if (!xe_dxil_test_write(&storage, 1, 0, 15, values, NULL)) return 0;
  memset(&io, 0, sizeof(io));
  io.context = &storage; io.read = xe_dxil_test_read; io.write = xe_dxil_test_write;
  io.kill = instruction_kill;
  io.loop_address = dxil_module_get_int32_const(m, 2); io.current_block = &block;
  xe_dxil_alu_init(&alu, m);
  if (xe_dxil_decode_alu(invalid, 3, &instruction) ||
      xe_dxil_decode_alu(instruction_cases[0].words, 2, &instruction) ||
      xe_dxil_decode_alu(NULL, 3, &instruction)) return 0;
  for (i = 0; i < ARRAY_SIZE(instruction_cases); ++i, ++*index) {
    if (!xe_dxil_decode_alu(instruction_cases[i].words, 3, &instruction) ||
        !xe_dxil_emit_alu_instruction(&alu, &instruction, &io,
          instruction_cases[i].body, instruction_cases[i].body + 1)) return 0;
    for (j = 0; j < 4; ++j) {
      pointer = xe_dxil_test_pointer(&storage, instruction_cases[i].exporting ?
        storage.exports : storage.registers, instruction_cases[i].destination, j, NULL);
      values[j] = pointer ? dxil_emit_load(m, pointer, 4, 0) : NULL;
      expected[j] = xe_dxil_test_float_bits(instruction_cases[i].result[j]);
    }
    expected[4] = instruction_cases[i].predicate;
    expected[5] = (uint32_t)instruction_cases[i].address; expected[6] = instruction_cases[i].killed;
    expected[7] = UINT32_C(0x60000000) | i;
    if (!xe_dxil_test_record(m, handle, *index, &alu, values, expected, golden)) return 0;
  }
  pointer = dxil_emit_load(m, storage.kill_count, 4, 0);
  if (!pointer) return 0;
  values[0] = dxil_emit_cast(m, DXIL_CAST_UITOFP, dxil_module_get_float_type(m, 32), pointer);
  for (j = 1; j < 4; ++j) values[j] = values[0];
  for (j = 0; j < 4; ++j) expected[j] = xe_dxil_test_float_bits(1.0f);
  expected[4] = 0; expected[5] = 4; expected[6] = 1; expected[7] = UINT32_C(0x6fffffff);
  if (!xe_dxil_test_record(m, handle, *index, &alu, values, expected, golden)) return 0;
  ++*index;
  return 1;
}

int xe_dxil_alu_test(struct dxil_module *m, const char *path) {
  static const float inputs[3][4] = {{2,-3,0,4},{5,2,-1,-2},{7,8,9,10}};
  XeDxilValue args[5];
  XeDxilValue handle;
  XeDxilValue operands[3][4];
  XeDxilValue values[4];
  XeDxilValue a;
  XeDxilValue b;
  XeDxilAlu alu;
  const struct dxil_func *function;
  uint32_t expected[8];
  char *expected_path;
  FILE *golden;
  unsigned index = 0;
  unsigned i;
  unsigned j;
  unsigned k;
  int result = 0;
  size_t length = strlen(path);
  if (length > (size_t)-1 - 10) return 0;
  expected_path = (char *)malloc(length + 10);
  if (!expected_path) return 0;
  memcpy(expected_path, path, length); memcpy(expected_path + length, ".expected", 10);
  golden = fopen(expected_path, "wb"); free(expected_path);
  if (!golden) return 0;
  args[0] = dxil_module_get_int32_const(m, 57);
  args[1] = dxil_module_get_int8_const(m, DXIL_RESOURCE_CLASS_UAV);
  args[2] = dxil_module_get_int32_const(m, 0); args[3] = args[2];
  args[4] = dxil_module_get_int1_const(m, 0);
  function = dxil_get_function(m, "dx.op.createHandle", DXIL_NONE);
  if (!function) goto done;
  handle = dxil_emit_call(m, function, args, 5);
  if (!handle) goto done;
  for (i = 0; i < ARRAY_SIZE(vector_cases); ++i, ++index) {
    xe_dxil_alu_init(&alu, m);
    for (j = 0; j < 3; ++j) for (k = 0; k < 4; ++k)
      operands[j][k] = xe_dxil_alu_float_bits(&alu, xe_dxil_test_float_bits(inputs[j][k]));
    if (!xe_dxil_alu_vector(&alu, vector_cases[i].opcode, operands, 0, values)) goto done;
    for (k = 0; k < 4; ++k) expected[k] = xe_dxil_test_float_bits(vector_cases[i].result[k]);
    expected[4] = vector_cases[i].predicate;
    expected[5] = (uint32_t)vector_cases[i].address; expected[6] = vector_cases[i].killed;
    expected[7] = UINT32_C(0x10000000) | vector_cases[i].opcode;
    if (!xe_dxil_test_record(m, handle, index, &alu, values, expected, golden)) goto done;
  }
  for (i = 0; i < ARRAY_SIZE(scalar_cases); ++i, ++index) {
    xe_dxil_alu_init(&alu, m);
    alu.previous_scalar = xe_dxil_alu_float_bits(&alu, xe_dxil_test_float_bits(2.0f));
    a = xe_dxil_alu_float_bits(&alu, xe_dxil_test_float_bits(scalar_cases[i].opcode >= 48 &&
      scalar_cases[i].opcode <= 49 ? 0.0f : 4.0f));
    b = xe_dxil_alu_float_bits(&alu, xe_dxil_test_float_bits(-3.0f));
    values[0] = xe_dxil_alu_scalar(&alu, scalar_cases[i].opcode, a, b, 0);
    for (k = 0; k < 4; ++k) { values[k] = values[0]; expected[k] = xe_dxil_test_float_bits(scalar_cases[i].result); }
    expected[4] = scalar_cases[i].predicate;
    expected[5] = (uint32_t)scalar_cases[i].address; expected[6] = scalar_cases[i].killed;
    expected[7] = UINT32_C(0x20000000) | scalar_cases[i].opcode;
    if (!xe_dxil_test_record(m, handle, index, &alu, values, expected, golden)) goto done;
  }
  for (i = 0; i < ARRAY_SIZE(extra_vectors); ++i, ++index) {
    xe_dxil_alu_init(&alu, m);
    for (j = 0; j < 3; ++j) for (k = 0; k < 4; ++k)
      operands[j][k] = xe_dxil_alu_float_bits(&alu, xe_dxil_test_float_bits(extra_vectors[i].inputs[j][k]));
    if (!xe_dxil_alu_vector(&alu, extra_vectors[i].opcode, operands, 0, values)) goto done;
    for (k = 0; k < 4; ++k) expected[k] = xe_dxil_test_float_bits(extra_vectors[i].result[k]);
    expected[4] = extra_vectors[i].predicate; expected[5] = (uint32_t)extra_vectors[i].address;
    expected[6] = extra_vectors[i].killed; expected[7] = UINT32_C(0x40000000) | i;
    if (!xe_dxil_test_record(m, handle, index, &alu, values, expected, golden)) goto done;
  }
  for (i = 0; i < ARRAY_SIZE(bit_vectors); ++i, ++index) {
    xe_dxil_alu_init(&alu, m);
    for (j = 0; j < 3; ++j) for (k = 0; k < 4; ++k)
      operands[j][k] = xe_dxil_alu_float_bits(&alu, bit_vectors[i].inputs[j][k]);
    if (!xe_dxil_alu_vector(&alu, bit_vectors[i].opcode, operands, 0, values)) goto done;
    for (k = 0; k < 4; ++k) expected[k] = bit_vectors[i].result[k];
    expected[4] = 0; expected[5] = 0; expected[6] = bit_vectors[i].killed;
    expected[7] = UINT32_C(0x50000000) | i;
    if (!xe_dxil_test_record(m, handle, index, &alu, values, expected, golden)) goto done;
  }
  for (i = 0; i < ARRAY_SIZE(boundaries); ++i, ++index) {
    xe_dxil_alu_init(&alu, m);
    alu.previous_scalar = xe_dxil_alu_float_bits(&alu, boundaries[i].previous);
    alu.mulsc_round_toward_zero = boundaries[i].round_toward_zero;
    a = xe_dxil_alu_float_bits(&alu, boundaries[i].a);
    b = xe_dxil_alu_float_bits(&alu, boundaries[i].b);
    values[0] = xe_dxil_alu_scalar(&alu, boundaries[i].opcode, a, b, boundaries[i].identical);
    for (k = 0; k < 4; ++k) { values[k] = values[0]; expected[k] = boundaries[i].result; }
    expected[4] = boundaries[i].predicate;
    expected[5] = (uint32_t)boundaries[i].address; expected[6] = boundaries[i].killed;
    expected[7] = UINT32_C(0x30000000) | i;
    if (!xe_dxil_test_record(m, handle, index, &alu, values, expected, golden)) goto done;
  }
  if (!instruction_test(m, handle, &index, golden)) goto done;
  xe_dxil_alu_init(&alu, m);
  if (xe_dxil_alu_scalar(&alu, 41, NULL, NULL, 0) || !alu.failed) goto done;
  xe_dxil_alu_init(&alu, m);
  if (xe_dxil_alu_vector(&alu, 30, operands, 0, values) || !alu.failed) goto done;
  result = dxil_emit_ret_void(m);
done:
  if (fclose(golden)) result = 0;
  return result;
}
