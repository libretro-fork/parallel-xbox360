/* Test-only constructor allocation observer implemented by the C89 driver. */
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void *xe_analysis_text_allocate(size_t);
#ifdef __cplusplus
}
#endif
#define XE_STRING_BUFFER_ALLOCATE xe_analysis_text_allocate
