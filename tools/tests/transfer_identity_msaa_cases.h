/* Independent physical sample payloads and legacy routing. BSD; MSVC C89.
 * Test-only integer aliases seed/read raw codes; the transfer uses its normal
 * numeric view. Never seed through the transfer routing under test. */
#ifndef XE_TEST_TRANSFER_IDENTITY_MSAA_CASES_H
#define XE_TEST_TRANSFER_IDENTITY_MSAA_CASES_H
#include "libretro/core_transfer_identity.h"
#include "transfer_address_oracle.h"
static INLINE uint32_t identity_ms_mask(unsigned view)
{
 return view==0?255:view==1?65535:1023;
}
static INLINE uint32_t identity_ms_alpha_mask(unsigned view)
{
 return view==2?3:identity_ms_mask(view);
}
static INLINE uint32_t identity_ms_base(unsigned view){return view==3?0x3800:0;}
static INLINE uint32_t identity_ms_pattern(unsigned view,unsigned x,unsigned y,
 unsigned sample,unsigned lane)
{
 uint32_t value;
 switch(lane) {
  case 0:value=0x1357u^x^(y<<3)^sample;break;
  case 1:value=0x2468u^(x<<1)^y^(sample<<4);break;
  case 2:value=0x369cu^(x<<2)^(y<<1)^(sample<<6);break;
  default:value=0x48adu^x^(y<<2)^sample;break;
 }
 return (value&(lane==3?identity_ms_alpha_mask(view):identity_ms_mask(view)))|identity_ms_base(view);
}
static INLINE int identity_ms_active(const xe_transfer_address_key *key,unsigned sample)
{
 return key->dest_msaa==0?sample==0:key->dest_msaa==1?
  (key->dest_native2?sample<2:(sample==0 || sample==3)):sample<4;
}
static INLINE void identity_ms_expected(const xe_transfer_address_key *key,unsigned view,
 unsigned x,unsigned y,unsigned sample,uint32_t out[4])
{
 uint32_t route[10];unsigned lane;
 if(identity_ms_active(key,sample)) {
  legacy(key,x,y,sample,route);
  for(lane=0;lane<4;++lane)out[lane]=identity_ms_pattern(view,route[0],route[1],route[2],lane);
 } else for(lane=0;lane<4;++lane)
  out[lane]=(7&(lane==3?identity_ms_alpha_mask(view):identity_ms_mask(view)))|identity_ms_base(view);
}
#endif
