/* Owned PE section snapshots, independent of mutable guest header bytes. */
#ifndef XE_CORE_PE_SECTION_H
#define XE_CORE_PE_SECTION_H
#include <stdint.h>
typedef struct xe_pe_section {
  char name[9];
  uint32_t raw_address, raw_size, address, size, flags;
} xe_pe_section;
#endif
