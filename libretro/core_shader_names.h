/* Bounded shader resource names, written directly in caller storage. C89.
 * The SPIR-V builder copies these names synchronously into owned operands. */
#ifndef XE_CORE_SHADER_NAMES_H
#define XE_CORE_SHADER_NAMES_H
#include <stdint.h>
#include <string.h>
#include <retro_inline.h>

/* Longest sampler name: 10 prefix + 10 fetch + 1 separator + 3 filters +
 * 2 anisotropy prefix + 10 ratio + 1 terminator = 37 bytes. */
#define XE_SHADER_NAME_CAPACITY 64

typedef enum xe_shader_indexed_name_kind {
   XE_SHADER_NAME_MEMEXPORT,
   XE_SHADER_NAME_OUTPUT,
   XE_SHADER_NAME_RECT_INPUT,
   XE_SHADER_NAME_INPUT,
   XE_SHADER_NAME_INPUT_PER_VERTEX
} xe_shader_indexed_name_kind;

static INLINE char *xe_shader_name_uint(char *out,uint32_t value)
{
   uint32_t divisor=1;
   while(value/divisor>=10)divisor*=10;
   do {
      *out++=(char)('0'+value/divisor);
      value%=divisor;
      divisor/=10;
   }while(divisor);
   return out;
}

/* Longest indexed prefix is 31 bytes, plus 10 digits and the terminator.
 * Other indexed names may add the 11-byte per-vertex suffix. */
static INLINE void xe_shader_indexed_name(char out[XE_SHADER_NAME_CAPACITY],
      uint32_t index,xe_shader_indexed_name_kind kind)
{
   const char *prefix;
   size_t size;
   char *end;
   switch(kind) {
      case XE_SHADER_NAME_OUTPUT: prefix="xe_out_interpolator_";break;
      case XE_SHADER_NAME_RECT_INPUT: prefix="xe_var_rect_guest_interpolators_";break;
      case XE_SHADER_NAME_INPUT:
      case XE_SHADER_NAME_INPUT_PER_VERTEX: prefix="xe_in_interpolator_";break;
      default: prefix="xe_var_memexport_data_";break;
   }
   size=strlen(prefix);memcpy(out,prefix,size);
   end=xe_shader_name_uint(out+size,index);
   if(kind==XE_SHADER_NAME_INPUT_PER_VERTEX) {
      memcpy(end,"_per_vertex",11);end+=11;
   }
   *end=0;
}

/* Dimension 0: 2D array, 1: 3D, 2: cube. Other values retain the 2D default. */
static INLINE void xe_shader_texture_name(char out[XE_SHADER_NAME_CAPACITY],
      uint32_t fetch,unsigned dimension,int is_signed)
{
   char *end;
   memcpy(out,"xe_texture",10);
   end=xe_shader_name_uint(out+10,fetch);
   *end++='_';
   if(dimension==2) {
      memcpy(end,"cube",4);end+=4;
   }else {
      *end++=dimension==1?'3':'2';*end++='d';
   }
   *end++='_';*end++=is_signed?'s':'u';*end=0;
}

/* Filter characters come from the typed translator's existing enum table.
 * Ratio is the already-clamped hardware setting; any uint32 fits this bound. */
static INLINE void xe_shader_sampler_name(char out[XE_SHADER_NAME_CAPACITY],
      uint32_t fetch,char mag,char min,char mip,int use_aniso,uint32_t ratio)
{
   char *end;
   memcpy(out,"xe_sampler",10);
   end=xe_shader_name_uint(out+10,fetch);
   *end++='_';*end++=mag;*end++=min;*end++=mip;
   if(use_aniso) {
      *end++='_';*end++='a';end=xe_shader_name_uint(end,ratio);
   }
   *end=0;
}
#endif
