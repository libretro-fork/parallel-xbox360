/* Independent numeric render-color reference. MSVC C89; BSD license. */
#ifndef XE_TEST_RENDER_COLOR_REFERENCE_H
#define XE_TEST_RENDER_COLOR_REFERENCE_H
#include <stdint.h>
#include <string.h>
#include <math.h>
static uint32_t color_bits(float v) { uint32_t b; memcpy(&b,&v,4); return b; }
static float color_float(uint32_t b) { float v; memcpy(&v,&b,4); return v; }
static float color_clamp(float v, float low, float high) { if (v != v) return 0; return v < low ? low : v > high ? high : v; }
static uint32_t color_even(double v) { double f = floor(v), d = v-f; uint32_t n = (uint32_t)f; return n+(d>.5 || (d==.5 && (n&1))); }
static unsigned color_components(unsigned f) { return f == 14 ? 1 : f == 4 || f == 6 || f == 15 ? 2 : 4; }
static unsigned color_words(unsigned f) { return f == 5 || f == 7 || f == 15 ? 2 : 1; }
static float color_half_decode(unsigned h) { unsigned e = (h>>10)&31, m = h&1023; float v = (float)ldexp((double)(e ? 1024+m : m),e ? (int)e-25 : -24); return h&32768 ? -v : v; }
static unsigned color_half_encode(float v) {
  uint32_t bits, mant, n, remainder, midpoint; int e, shift; unsigned sign;
  v = color_clamp(v,-131008,131008); bits = color_bits(v); sign = (bits>>16)&32768;
  e = (int)((bits>>23)&255)-127; mant = bits&0x7fffff;
  if (e < -25) return sign;
  if (e >= -14) { n = ((uint32_t)(e+15)<<10)+(mant>>13); remainder = mant&8191; midpoint = 4096; }
  else { shift = -e-1; mant |= 0x800000; n = mant>>shift; remainder = mant&((1u<<shift)-1); midpoint = 1u<<(shift-1); }
  n += remainder > midpoint || (remainder == midpoint && (n&1));
  return sign | (n > 32767 ? 32767 : n);
}
static float color_gamma_decode(unsigned b) {
  float g = (float)b*(1.0f/255.0f), scale = 1.0f/1024, offset = 0, linear, term;
  if (g >= 64.0f/255) { scale = 2.0f/1024; offset = -64; }
  if (g >= 96.0f/255) { scale = 4.0f/1024; offset = -256; }
  if (g >= 192.0f/255) { scale = 8.0f/1024; offset = -1024; }
  linear = g*(255.0f*1024); linear *= scale; linear += offset;
  term = linear*scale; term = (float)(int)term;
  return (linear+term)*(1.0f/1023);
}
static unsigned color_gamma_encode(float v) {
  float scale = 1023, offset = 0, t;
  if (v >= 64.0f/1023) { scale = 511.5f; offset = 32.0f/255; }
  if (v >= 128.0f/1023) { scale = 255.75f; offset = 64.0f/255; }
  if (v >= 512.0f/1023) { scale = 127.875f; offset = 128.0f/255; }
  t = (float)(int)(v*scale); t *= 1.0f/255; t += offset; t *= 255; t += .5f;
  return (unsigned)t;
}
static void color_unpack(unsigned f, const uint32_t p[2], float v[4]) {
  unsigned i, n, e, m, width, shift;
  for (i = 0; i < 4; ++i) {
    v[i] = i == 3 ? 1.0f : 0.0f; if (i >= color_components(f)) continue;
    if (f >= 14) { v[i] = color_float(p[i]); continue; }
    if (f >= 4 && f <= 7) {
      n = (p[i/2]>>((i&1)*16))&65535;
      if (f >= 6) v[i] = color_half_decode(n);
      else { v[i] = (float)(n&32768 ? (int)n-65536 : (int)n)*(32.0f/32767); if (v[i] < -32) v[i] = -32; }
    } else {
      width = f < 2 ? 8 : i < 3 ? 10 : 2; shift = f < 2 ? i*8 : i*10;
      n = (p[0]>>shift)&((1u<<width)-1);
      if ((f == 3 || f == 12) && i < 3) { e = n>>7; m = n&127; v[i] = (float)ldexp((double)(e ? 128+m : m),e ? (int)e-10 : -9); }
      else v[i] = f == 1 && i < 3 ? color_gamma_decode(n) : (float)n*(1.0f/(float)((1u<<width)-1));
    }
  }
}
static void color_pack(unsigned f, const float v[4], uint32_t p[2]) {
  unsigned i, n, width, shift; int e; float t, a;
  p[0] = p[1] = 0;
  for (i = 0; i < color_components(f); ++i) {
    if (f >= 14) { p[i] = color_bits(v[i]); continue; }
    if (f >= 4 && f <= 7) {
      if (f >= 6) n = color_half_encode(v[i]);
      else { t = color_clamp(v[i],-32,32)*(32767.0f/32); n = (uint32_t)(int)(t+(t < 0 ? -.5f : .5f)); }
      p[i/2] |= (n&65535)<<((i&1)*16); continue;
    }
    width = f < 2 ? 8 : i < 3 ? 10 : 2; shift = f < 2 ? i*8 : i*10;
    t = color_clamp(v[i],0,(f == 3 || f == 12) && i < 3 ? 31.875f : 1);
    if ((f == 3 || f == 12) && i < 3) {
      if (t < .25f) n = color_even((double)t*512);
      else { frexp((double)t,&e); a = (float)ldexp((double)t,8-e)-128; n = ((unsigned)(e+2)<<7)+color_even(a); }
    } else if (f == 1 && i < 3) n = color_gamma_encode(t);
    else { t *= (float)((1u<<width)-1); t += .5f; n = (unsigned)t; }
    p[0] |= (n&((1u<<width)-1))<<shift;
  }
}
#endif
