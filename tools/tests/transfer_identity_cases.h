/* Procedural image/sample/scale setup, no game assets. BSD; C89. */
#ifndef XE_TEST_TRANSFER_IDENTITY_CASES_H
#define XE_TEST_TRANSFER_IDENTITY_CASES_H
#include "libretro/core_transfer_identity.h"
#include <string.h>
static void identity_case(xe_transfer_address_key *key,xe_transfer_identity_options *options,
 unsigned native2,unsigned dest,unsigned source,unsigned scale,unsigned target,unsigned manual)
{
 memset(key,0,sizeof(*key));memset(options,0,sizeof(*options));
 key->dest_msaa=dest;key->source_msaa=source;
 key->dest_native2=key->source_native2=native2;
 key->dest_scale_x=scale&2?1:2;key->dest_scale_y=scale&2?1:3;
 key->source_scale_x=scale&1?1:2;key->source_scale_y=scale&1?1:3;
 key->dest_color=key->source_color=1;
 key->dest_pitch=17;key->source_pitch=31;key->dest_to_source_base=-7;
 options->source_multisampled=source!=0;options->target=target;
 options->sample_mode=!dest?XE_TRANSFER_IDENTITY_SINGLE_SAMPLE:
  manual?XE_TRANSFER_IDENTITY_SAMPLE_CONSTANT:XE_TRANSFER_IDENTITY_SAMPLE_BUILTIN;
}
#endif
