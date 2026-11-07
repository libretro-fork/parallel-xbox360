/* Forced only into the test build of edram_dump.c, never into release. */
#include "dxil_module.h"
const struct dxil_value *xe_test_resolve_call(struct dxil_module *, const struct dxil_func *,
    const struct dxil_value **, size_t);
#undef dxil_emit_call
#define dxil_emit_call xe_test_resolve_call
const struct dxil_value *xe_test_resolve_binop(struct dxil_module *, enum dxil_bin_opcode,
    const struct dxil_value *, const struct dxil_value *, enum dxil_opt_flags);
const struct dxil_value *xe_test_resolve_extract(struct dxil_module *,
    const struct dxil_value *, unsigned);
#undef dxil_emit_binop
#define dxil_emit_binop xe_test_resolve_binop
#undef dxil_emit_extractval
#define dxil_emit_extractval xe_test_resolve_extract
