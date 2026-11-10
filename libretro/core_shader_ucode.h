/* Immutable shader snapshot storage. BSD license. MSVC C89.
 * Allocate once, fill the final words before publication, borrow until close.
 * Guest/file scratch memory cannot be borrowed after its producer reuses it. */
#ifndef XE_CORE_SHADER_UCODE_H
#define XE_CORE_SHADER_UCODE_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <retro_inline.h>
#ifndef XE_SHADER_UCODE_ALLOC
#define XE_SHADER_UCODE_ALLOC malloc
#endif
#ifndef XE_SHADER_UCODE_FREE
#define XE_SHADER_UCODE_FREE free
#endif
typedef struct xe_shader_ucode {
 uint32_t *words;
 size_t count;
} xe_shader_ucode;
#define XE_SHADER_UCODE_LARGE_ALIGNMENT 32
/* Requires an empty owner. Failure preserves it; empty input allocates nothing.
 * Payload is intentionally uninitialized, not observable until fully filled.
 * Large backing blocks include bounded alignment/pointer-prefix overhead. */
static INLINE int xe_shader_ucode_allocate(xe_shader_ucode *out,size_t count) {
 uint32_t *words;void *allocation;size_t bytes,overhead;
 if(!out || out->words || out->count || count>(size_t)-1/sizeof(uint32_t))return 0;
 if(!count)return 1;
 bytes=count*sizeof(uint32_t);overhead=bytes>=4096?31+sizeof(void*):0;
 if(bytes>(size_t)-1-overhead)return 0;
 allocation=XE_SHADER_UCODE_ALLOC(bytes+overhead);if(!allocation)return 0;
 words=(uint32_t*)allocation;
 if(overhead){
  /* Preserve the 32-byte large-block alignment used by the MSVC vector path,
   * without initializing payload bytes. The prefix is outside the word view. */
  words=(uint32_t*)(((uintptr_t)allocation+sizeof(void*)+31)&~(uintptr_t)31);
  memcpy((unsigned char*)words-sizeof(void*),&allocation,sizeof(void*));
 }
 out->words=words;out->count=count;return 1;
}
/* All consumers must be quiescent. Close is idempotent; the owner can reopen. */
static INLINE void xe_shader_ucode_close(xe_shader_ucode *out) {
 void *allocation;
 if(!out)return;
 allocation=out->words;
 if(out->words && out->count>=4096/sizeof(uint32_t))
  memcpy(&allocation,(unsigned char*)out->words-sizeof(void*),sizeof(void*));
 XE_SHADER_UCODE_FREE(allocation);out->words=NULL;out->count=0;
}
#endif
