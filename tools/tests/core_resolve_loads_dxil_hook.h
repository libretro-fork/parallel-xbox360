/* Forced only into the test build of edram_dump.c, never into release. */
#include "dxil_module.h"
const struct dxil_value *xe_test_resolve_call(struct dxil_module *, const struct dxil_func *,
    const struct dxil_value **, size_t);
#undef dxil_emit_call
#define dxil_emit_call xe_test_resolve_call
