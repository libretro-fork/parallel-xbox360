/* Procedural measurement ABI only; C89. */
#ifndef XE_TEST_TRANSFER_REFERENCE_H
#define XE_TEST_TRANSFER_REFERENCE_H
#include "libretro/core_spv_binary.h"
typedef struct xe_test_transfer_reference_options {
 unsigned source_format,dest_format,source_msaa,dest_msaa;
 unsigned source_native,dest_native,scale_x,scale_y,native2,shading;
} xe_test_transfer_reference_options;
#ifdef __cplusplus
extern "C" {
#endif
int xe_test_transfer_reference_build(xe_spv_binary *out,const xe_test_transfer_reference_options *options);
#ifdef __cplusplus
}
#endif
#endif