/* Independent test-only numeric nearest-neighbor reference. MSVC C89. */
#ifndef XE_TEST_TRANSFER_SMALL_FLOAT_REFERENCE_H
#define XE_TEST_TRANSFER_SMALL_FLOAT_REFERENCE_H
#include <stdint.h>
#include <string.h>
#include <math.h>
static INLINE uint32_t ref_half_f32_bits(unsigned half){
 unsigned sign=half>>15,exponent=(half>>10)&31,mantissa=half&1023;
 uint32_t result;float value;
 if(exponent==31)return (sign<<31)|UINT32_C(0x7f800000)|(mantissa<<13);
 value=exponent?(float)ldexp((double)(1024+mantissa),(int)exponent-25):(float)ldexp((double)mantissa,-24);
 if(sign)value=-value;
 memcpy(&result,&value,4);return result;
}
static INLINE double ref_7e3_value(unsigned code){
 unsigned exponent=code/128,mantissa=code%128;
 return exponent?ldexp((double)(128+mantissa),(int)exponent-10):(double)mantissa/512.0;
}
static INLINE unsigned ref_7e3_pack_bits(uint32_t bits){
 float f;double value,lower,upper;unsigned lo=0,hi=1023,mid;
 memcpy(&f,&bits,4);value=(double)f;
 if(!(value>0))return 0;
 if(value>=31.875)return 1023;
 while(hi-lo>1){mid=(lo+hi)/2;if(value<ref_7e3_value(mid))hi=mid;else lo=mid;}
 lower=value-ref_7e3_value(lo);upper=ref_7e3_value(hi)-value;
 return lower<upper?lo:upper<lower?hi:(lo&1?hi:lo);
}
enum {XE_TEST_7E3_ROUNDING_CASES=65536+1023*3};
static INLINE uint32_t ref_7e3_rounding_input(unsigned index){
 float midpoint;uint32_t bits;unsigned code;
 if(index<65536)return ref_half_f32_bits(index);
 index-=65536;code=index/3;
 midpoint=(float)((ref_7e3_value(code)+ref_7e3_value(code+1))*.5);
 memcpy(&bits,&midpoint,4);return index%3==0?bits-1:index%3==1?bits:bits+1;
}
#endif
