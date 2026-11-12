/* Raw host-view encodings with independent legacy EDRAM routes. BSD; C89. */
#ifndef XE_TEST_TRANSFER_IDENTITY_IMAGE_CASES_H
#define XE_TEST_TRANSFER_IDENTITY_IMAGE_CASES_H
#include "libretro/core_transfer_identity.h"
#include "transfer_address_oracle.h"
static INLINE unsigned identity_components(unsigned view){return view==2?1:4;}
static INLINE unsigned identity_bytes(unsigned view){return view==0?1:view==2?4:2;}
static INLINE uint32_t identity_pattern(unsigned view,unsigned x,unsigned y,unsigned lane)
{
 uint32_t p=x+y*320,v;
 static const uint32_t alpha[4]={0,0x3555,0x3955,0x3c00};
 if(view==0)return (p+lane*61)&255;
 if(view==1)return (p+lane*8191)&65535;
 if(view==2)return (p&1023)|(((p+341)&1023)<<10)|(((p+683)&1023)<<20)|(((p>>10)&3)<<30);
 if(lane==3)return alpha[p&3];
 v=(p+lane*8191)%0x3400+0x1800;
 if(!(p&31))v=0;
 if((p&31)==1)v=0x8000;
 return v;
}
static INLINE void identity_expected(const xe_transfer_address_key *key,unsigned view,
 unsigned x,unsigned y,uint32_t out[4])
{
 uint32_t route[10];unsigned lane;
 legacy(key,x,y,0,route);
 for(lane=0;lane<4;++lane)out[lane]=identity_pattern(view,route[0],route[1],lane);
}
#endif
