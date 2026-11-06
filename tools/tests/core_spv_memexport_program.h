#ifndef XE_SPV_MEMEXPORT_TEST_PROGRAM_H
#define XE_SPV_MEMEXPORT_TEST_PROGRAM_H
#include <stdint.h>
#include <stddef.h>
/* Packed fields follow ucode.h's ControlFlowAlloc/Exec and AluInstruction.
 * ALU eA = mad r0,c0,c1; eM# = max r0,r0. No private-field mutation. */
static size_t xe_spv_memexport_test_program(uint32_t *words, unsigned mask) {
    size_t count=0, i, offset;
    if (!mask) { words[0]=0; words[1]=UINT32_C(0x2000); words[2]=0; return 3; }
    for(i=0;i<5;++i) if(mask&(1u<<i)) ++count;
    ++count;
    words[0]=4;
    words[1]=UINT32_C(0xc600) | ((1u | ((uint32_t)count<<12))<<16);
    words[2]=UINT32_C(0x20000000);
    words[3]=32u | (1u<<15) | (15u<<16) | (50u<<26);
    words[4]=0; words[5]=1u | (11u<<24) | (1u<<31);
    offset=6;
    for(i=0;i<5;++i) if(mask&(1u<<i)) {
        words[offset++]=(uint32_t)(33+i) | (1u<<15) | (15u<<16) | (50u<<26);
        words[offset++]=0; words[offset++]=(2u<<24) | (3u<<30);
    }
    return offset;
}
#endif
