/* Direct production into queue storage; MSVC C89. */
#include "core_audio_convert.h"
#include <stdint.h>
#include <string.h>
#if defined(_M_X64) || defined(__x86_64__)
#include <tmmintrin.h>
#define XE_AUDIO_SSE 1
#elif defined(_M_ARM64) || defined(__aarch64__)
#include <arm_neon.h>
#define XE_AUDIO_NEON 1
#endif

static float read_be(const float *in) {
   uint32_t bits;
   float value;
   memcpy(&bits, in, sizeof(bits));
   bits = (bits >> 24) | ((bits >> 8) & UINT32_C(0xff00)) |
         ((bits << 8) & UINT32_C(0xff0000)) | (bits << 24);
   memcpy(&value, &bits, sizeof(value));
   return value;
}
#ifdef XE_AUDIO_SSE
static __m128 read_be4(const float *in) {
   return _mm_castsi128_ps(_mm_shuffle_epi8(_mm_castps_si128(_mm_loadu_ps(in)),
         _mm_set_epi8(12,13,14,15,8,9,10,11,4,5,6,7,0,1,2,3)));
}
#elif defined(XE_AUDIO_NEON)
static float32x4_t read_be4(const float *in) {
   return vreinterpretq_f32_u8(vrev32q_u8(vld1q_u8((const uint8_t *)in)));
}
#endif

void xe_audio_convert(float *out, const float *in, size_t total_frames,
      size_t offset, size_t count, unsigned channels, unsigned output_channels,
      int sequential_be, float volume) {
   size_t i = 0, c;
   float fl, fr, fc, lfe, sl, sr, value;
   const float *frame;
   if (sequential_be && output_channels == 2) {
#ifdef XE_AUDIO_SSE
      __m128 gain = _mm_set1_ps(0.707106781f), half = _mm_set1_ps(0.5f);
      __m128 vfl, vfr, vfc, vlfe, vsl, vsr, left, right;
      for (; i + 4 <= count; i += 4) {
         vfl = read_be4(in + offset + i);
         vfr = read_be4(in + total_frames + offset + i);
         vfc = _mm_mul_ps(read_be4(in + 2 * total_frames + offset + i), gain);
         vlfe = _mm_mul_ps(read_be4(in + 3 * total_frames + offset + i), half);
         vsl = _mm_mul_ps(read_be4(in + 4 * total_frames + offset + i), gain);
         vsr = _mm_mul_ps(read_be4(in + 5 * total_frames + offset + i), gain);
         left = _mm_add_ps(vfl, _mm_add_ps(vfc, _mm_add_ps(vsl, vlfe)));
         right = _mm_add_ps(vfr, _mm_add_ps(vfc, _mm_add_ps(vsr, vlfe)));
         if (volume != 1.0f) {
            left = _mm_mul_ps(left, _mm_set1_ps(volume));
            right = _mm_mul_ps(right, _mm_set1_ps(volume));
         }
         _mm_storeu_ps(out + i * 2, _mm_unpacklo_ps(left, right));
         _mm_storeu_ps(out + i * 2 + 4, _mm_unpackhi_ps(left, right));
      }
#elif defined(XE_AUDIO_NEON)
      float32x4_t gain = vdupq_n_f32(0.707106781f), half = vdupq_n_f32(0.5f);
      float32x4_t vfl, vfr, vfc, vlfe, vsl, vsr, left, right;
      float32x4x2_t pair;
      for (; i + 4 <= count; i += 4) {
         vfl = read_be4(in + offset + i);
         vfr = read_be4(in + total_frames + offset + i);
         vfc = vmulq_f32(read_be4(in + 2 * total_frames + offset + i), gain);
         vlfe = vmulq_f32(read_be4(in + 3 * total_frames + offset + i), half);
         vsl = vmulq_f32(read_be4(in + 4 * total_frames + offset + i), gain);
         vsr = vmulq_f32(read_be4(in + 5 * total_frames + offset + i), gain);
         left = vaddq_f32(vfl, vaddq_f32(vfc, vaddq_f32(vsl, vlfe)));
         right = vaddq_f32(vfr, vaddq_f32(vfc, vaddq_f32(vsr, vlfe)));
         if (volume != 1.0f) {
            left = vmulq_f32(left, vdupq_n_f32(volume));
            right = vmulq_f32(right, vdupq_n_f32(volume));
         }
         pair = vzipq_f32(left, right);
         vst1q_f32(out + i * 2, pair.val[0]);
         vst1q_f32(out + i * 2 + 4, pair.val[1]);
      }
#endif
      for (; i < count; ++i) {
         fl = read_be(in + offset + i);
         fr = read_be(in + total_frames + offset + i);
         fc = read_be(in + 2 * total_frames + offset + i) * 0.707106781f;
         lfe = read_be(in + 3 * total_frames + offset + i) * 0.5f;
         sl = read_be(in + 4 * total_frames + offset + i) * 0.707106781f;
         sr = read_be(in + 5 * total_frames + offset + i) * 0.707106781f;
         out[i * 2] = fl + (fc + (sl + lfe));
         out[i * 2 + 1] = fr + (fc + (sr + lfe));
         if (volume != 1.0f) {
            out[i * 2] *= volume; out[i * 2 + 1] *= volume;
         }
      }
   } else if (sequential_be) {
      for (; i < count; ++i)
         for (c = 0; c < 6; ++c)
         {
            value = read_be(in + c * total_frames + offset + i);
            out[i * 6 + c] = volume != 1.0f ? value * volume : value;
         }
   } else if (channels == output_channels) {
      if (volume == 1.0f) {
         memcpy(out, in + offset * channels, count * channels * sizeof(float));
         return;
      }
      for (; i < count * channels; ++i) out[i] = in[offset * channels + i] * volume;
      return;
   } else if (channels == 2) {
      for (; i < count; ++i) {
         out[i * 6] = in[(offset + i) * 2];
         out[i * 6 + 1] = in[(offset + i) * 2 + 1];
         value = volume != 1.0f ? 0.0f * volume : 0.0f;
         out[i * 6 + 2] = out[i * 6 + 3] = out[i * 6 + 4] = out[i * 6 + 5] = value;
         if (volume != 1.0f) {
            out[i * 6] *= volume; out[i * 6 + 1] *= volume;
         }
      }
   } else {
      /* Preserve the media-player matrix and operation order. */
      for (; i < count; ++i) {
         frame = in + (offset + i) * 6;
         value = frame[2] * 0.7071f;
         out[i * 2] = frame[0] + value + frame[4] * 0.7071f;
         out[i * 2 + 1] = frame[1] + value + frame[5] * 0.7071f;
         if (volume != 1.0f) {
            out[i * 2] *= volume; out[i * 2 + 1] *= volume;
         }
      }
   }
}
/* Add directly from a borrowed packet into the final mix, without staging. */
void xe_audio_mix_borrowed(float *out,const float *in,size_t total_frames,
      size_t offset,size_t count,unsigned channels,unsigned output_channels,
      int sequential_be,float volume) {
   size_t i=0,c;
   float fl,fr,fc,lfe,sl,sr,left,right,value;
   const float *frame;
   if (sequential_be && output_channels==2) {
#ifdef XE_AUDIO_SSE
      __m128 gain=_mm_set1_ps(0.707106781f),half=_mm_set1_ps(0.5f);
      __m128 vl,vr,vc,vlf,vsl,vsr,lo,hi;
      for (;i+4<=count;i+=4) {
         vl=read_be4(in+offset+i); vr=read_be4(in+total_frames+offset+i);
         vc=_mm_mul_ps(read_be4(in+2*total_frames+offset+i),gain);
         vlf=_mm_mul_ps(read_be4(in+3*total_frames+offset+i),half);
         vsl=_mm_mul_ps(read_be4(in+4*total_frames+offset+i),gain);
         vsr=_mm_mul_ps(read_be4(in+5*total_frames+offset+i),gain);
         vl=_mm_add_ps(vl,_mm_add_ps(vc,_mm_add_ps(vsl,vlf)));
         vr=_mm_add_ps(vr,_mm_add_ps(vc,_mm_add_ps(vsr,vlf)));
         if (volume!=1.0f) { vl=_mm_mul_ps(vl,_mm_set1_ps(volume));vr=_mm_mul_ps(vr,_mm_set1_ps(volume)); }
         lo=_mm_unpacklo_ps(vl,vr);hi=_mm_unpackhi_ps(vl,vr);
         _mm_storeu_ps(out+i*2,_mm_add_ps(_mm_loadu_ps(out+i*2),lo));
         _mm_storeu_ps(out+i*2+4,_mm_add_ps(_mm_loadu_ps(out+i*2+4),hi));
      }
#elif defined(XE_AUDIO_NEON)
      float32x4_t gain=vdupq_n_f32(0.707106781f),half=vdupq_n_f32(0.5f);
      float32x4_t vl,vr,vc,vlf,vsl,vsr;
      float32x4x2_t pair;
      for (;i+4<=count;i+=4) {
         vl=read_be4(in+offset+i);vr=read_be4(in+total_frames+offset+i);
         vc=vmulq_f32(read_be4(in+2*total_frames+offset+i),gain);
         vlf=vmulq_f32(read_be4(in+3*total_frames+offset+i),half);
         vsl=vmulq_f32(read_be4(in+4*total_frames+offset+i),gain);
         vsr=vmulq_f32(read_be4(in+5*total_frames+offset+i),gain);
         vl=vaddq_f32(vl,vaddq_f32(vc,vaddq_f32(vsl,vlf)));
         vr=vaddq_f32(vr,vaddq_f32(vc,vaddq_f32(vsr,vlf)));
         if (volume!=1.0f) {vl=vmulq_f32(vl,vdupq_n_f32(volume));vr=vmulq_f32(vr,vdupq_n_f32(volume));}
         pair=vzipq_f32(vl,vr);
         vst1q_f32(out+i*2,vaddq_f32(vld1q_f32(out+i*2),pair.val[0]));
         vst1q_f32(out+i*2+4,vaddq_f32(vld1q_f32(out+i*2+4),pair.val[1]));
      }
#endif
      for (;i<count;++i) {
         fl=read_be(in+offset+i);fr=read_be(in+total_frames+offset+i);
         fc=read_be(in+2*total_frames+offset+i)*0.707106781f;
         lfe=read_be(in+3*total_frames+offset+i)*0.5f;
         sl=read_be(in+4*total_frames+offset+i)*0.707106781f;
         sr=read_be(in+5*total_frames+offset+i)*0.707106781f;
         left=fl+(fc+(sl+lfe));right=fr+(fc+(sr+lfe));
         if (volume!=1.0f) {left*=volume;right*=volume;}
         out[i*2]+=left;out[i*2+1]+=right;
      }
   } else if (sequential_be) {
      for (;i<count;++i) for(c=0;c<6;++c) {
         value=read_be(in+c*total_frames+offset+i);
         out[i*6+c]+=volume!=1.0f?value*volume:value;
      }
   } else if (channels==output_channels) {
      for (;i<count*channels;++i) {
         value=in[offset*channels+i];
         out[i]+=volume!=1.0f?value*volume:value;
      }
   } else if (channels==2) {
      for (;i<count;++i) for(c=0;c<2;++c) {
         value=in[(offset+i)*2+c];
         out[i*6+c]+=volume!=1.0f?value*volume:value;
      }
   } else {
      for (;i<count;++i) {
         frame=in+(offset+i)*6;
         fc=frame[2]*0.7071f;
         left=frame[0]+fc+frame[4]*0.7071f;
         right=frame[1]+fc+frame[5]*0.7071f;
         if (volume!=1.0f) {left*=volume;right*=volume;}
         out[i*2]+=left;out[i*2+1]+=right;
      }
   }
}
