/* Static Xenos destination addressing for resolve emitters. MSVC C89.
 * Operands/results are borrowed native IR IDs; no per-dispatch work/storage. */
#ifndef XE_CORE_RESOLVE_TILED_ADDRESS_H
#define XE_CORE_RESOLVE_TILED_ADDRESS_H
#include <stdint.h>
#include "../third_party/libretro-common/include/retro_inline.h"
enum xe_resolve_address_op {
  XE_RESOLVE_ADDR_CONST, XE_RESOLVE_ADDR_ADD, XE_RESOLVE_ADDR_MUL,
  XE_RESOLVE_ADDR_SHL, XE_RESOLVE_ADDR_SHR, XE_RESOLVE_ADDR_AND,
  XE_RESOLVE_ADDR_OR, XE_RESOLVE_ADDR_XOR
};
typedef uintptr_t (*xe_resolve_address_fn)(void *, enum xe_resolve_address_op,
    uintptr_t, uintptr_t);
typedef struct xe_resolve_address_emitter {
  xe_resolve_address_fn emit;
  void *context;
  int failed;
} xe_resolve_address_emitter;
static INLINE uintptr_t xe_resolve_addr_op(xe_resolve_address_emitter *e,
    enum xe_resolve_address_op op, uintptr_t a, uintptr_t b) {
  uintptr_t result;
  if (e->failed) return 0;
  if (op != XE_RESOLVE_ADDR_CONST && (!a || !b)) { e->failed = 1; return 0; }
  result = e->emit(e->context,op,a,b);
  if (!result) e->failed = 1;
  return result;
}
static INLINE uintptr_t xe_resolve_addr_literal(xe_resolve_address_emitter *e,
    enum xe_resolve_address_op op, uintptr_t a, uint32_t literal) {
  uintptr_t b = xe_resolve_addr_op(e,XE_RESOLVE_ADDR_CONST,literal,0);
  return xe_resolve_addr_op(e,op,a,b);
}
static INLINE uintptr_t xe_resolve_tiled_address(xe_resolve_address_fn emit,
    void *context, int volume, uintptr_t x, uintptr_t y, uintptr_t pitch_div32,
    uintptr_t slice, uintptr_t height_div32, unsigned bpp_log2) {
  xe_resolve_address_emitter state;
  uintptr_t outer, inner, bytes, bank, pipe, address, x3, x5, y1, y3, z2;
  if (!emit || !x || !y || !pitch_div32 || (volume && (!slice || !height_div32)) ||
      bpp_log2 > 4) return 0;
  state.emit = emit; state.context = context; state.failed = 0;
#define XE_RA_OP(op,a,b) xe_resolve_addr_op(&state,XE_RESOLVE_ADDR_##op,a,b)
#define XE_RA_IMM(op,a,b) xe_resolve_addr_literal(&state,XE_RESOLVE_ADDR_##op,a,b)
  x3 = XE_RA_IMM(SHR,x,3); x5 = XE_RA_IMM(SHR,x,5);
  y1 = XE_RA_IMM(SHR,y,1); y3 = XE_RA_IMM(SHR,y,3);
  if (volume) {
    z2 = XE_RA_IMM(SHR,slice,2);
    outer = XE_RA_IMM(SHL,XE_RA_OP(ADD,XE_RA_OP(MUL,XE_RA_OP(ADD,
        XE_RA_OP(MUL,z2,XE_RA_IMM(SHL,height_div32,1)),XE_RA_IMM(SHR,y,4)),pitch_div32),x5),7);
    inner = XE_RA_OP(OR,XE_RA_OP(OR,XE_RA_IMM(SHL,XE_RA_IMM(AND,slice,3),5),
        XE_RA_IMM(SHL,XE_RA_IMM(AND,y1,3),3)),XE_RA_IMM(AND,x,7));
    bank = XE_RA_IMM(AND,XE_RA_OP(XOR,y3,z2),1);
    pipe = XE_RA_OP(XOR,XE_RA_IMM(AND,x3,3),XE_RA_IMM(SHL,bank,1));
  } else {
    outer = XE_RA_IMM(SHL,XE_RA_OP(ADD,XE_RA_OP(MUL,XE_RA_IMM(SHR,y,5),pitch_div32),x5),6);
    inner = XE_RA_OP(OR,XE_RA_IMM(SHL,XE_RA_IMM(AND,y1,7),3),XE_RA_IMM(AND,x,7));
    bank = XE_RA_IMM(AND,XE_RA_IMM(SHR,y,4),1);
    pipe = XE_RA_OP(XOR,XE_RA_IMM(AND,x3,3),XE_RA_IMM(SHL,XE_RA_IMM(AND,y3,1),1));
  }
  bytes = XE_RA_IMM(SHL,XE_RA_OP(OR,outer,inner),bpp_log2);
  address = XE_RA_OP(OR,XE_RA_OP(OR,XE_RA_IMM(SHL,XE_RA_IMM(AND,y,1),4),XE_RA_IMM(SHL,pipe,6)),XE_RA_IMM(SHL,bank,11));
  address = XE_RA_OP(OR,address,XE_RA_IMM(AND,bytes,15));
  address = XE_RA_OP(OR,address,XE_RA_IMM(SHL,XE_RA_IMM(AND,XE_RA_IMM(SHR,bytes,4),1),5));
  address = XE_RA_OP(OR,address,XE_RA_IMM(SHL,XE_RA_IMM(AND,XE_RA_IMM(SHR,bytes,5),7),8));
  address = XE_RA_OP(OR,address,XE_RA_IMM(SHL,XE_RA_IMM(SHR,bytes,8),12));
#undef XE_RA_IMM
#undef XE_RA_OP
  return state.failed ? 0 : address;
}
#endif
