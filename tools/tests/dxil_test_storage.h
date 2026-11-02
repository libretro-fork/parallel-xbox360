/* Shared native DXIL fixture storage. MSVC C89; BSD license. */
#ifndef XE_DXIL_TEST_STORAGE_H
#define XE_DXIL_TEST_STORAGE_H
#include "guest_alu.h"
#include <stdio.h>

typedef struct InstructionStorage {
  struct dxil_module *module;
  XeDxilValue registers;
  XeDxilValue exports;
  XeDxilValue kill_count;
  unsigned *current_block;
  unsigned next_kill_block;
} InstructionStorage;

uint32_t xe_dxil_test_float_bits(float value);
XeDxilValue xe_dxil_test_pointer(InstructionStorage *storage, XeDxilValue base,
                                  unsigned index, unsigned component, XeDxilValue offset);
XeDxilValue xe_dxil_test_read(void *context, unsigned constant, unsigned index,
                               unsigned component, XeDxilValue offset);
int xe_dxil_test_write(void *context, unsigned exporting, unsigned index,
                        unsigned mask, XeDxilValue values[4], XeDxilValue offset);
int xe_dxil_test_record(struct dxil_module *module, XeDxilValue handle, unsigned index,
                         XeDxilAlu *alu, XeDxilValue values[4],
                         const uint32_t expected[8], FILE *golden);
#endif
