/* Optional trace I/O and borrowed compression input, MSVC C89.
 * One synchronous owner. Source views expire when their producer returns. */
#ifndef XE_CORE_GPU_TRACE_H
#define XE_CORE_GPU_TRACE_H
#include "core_cache_file.h"
#include <string.h>
#include <limits.h>
#include <file/file_path.h>
#include <retro_inline.h>
typedef struct xe_trace_stream { RFILE *file; int failed; } xe_trace_stream;
static INLINE int xe_trace_stream_close(xe_trace_stream *s) {
  int ok;
  if (!s) return 0;
  ok = !s->failed;
  if (s->file) {
    if (filestream_flush(s->file)) ok = 0;
    if (filestream_close(s->file)) ok = 0;
  }
  s->file = NULL; s->failed = 0; return ok;
}
static INLINE int xe_trace_stream_open(xe_trace_stream *s, const char *path) {
  char parent[4096];
  if (!s || !path || !*path || strlen(path)>=sizeof(parent)) return 0;
  xe_trace_stream_close(s);
  fill_pathname_basedir(parent,path,sizeof(parent));
  if (*parent && !path_mkdir(parent)) return 0;
  s->file = filestream_open(path,RETRO_VFS_FILE_ACCESS_WRITE,0);
  return s->file != NULL;
}
/* Trace lengths are u32 even when host size_t is wider. Preflight before
 * pointer arithmetic or compression, including optional borrowed host views. */
static INLINE int xe_trace_payload_size(xe_trace_stream *s,const void *data,
    uint64_t count,uint64_t stride,uint64_t available,uint32_t *out) {
  uint64_t bytes;
  if(!s || !out) return 0;
  if(!stride || count>UINT32_MAX/stride || count>available/stride ||
      (count && !data)) { s->failed=1; return 0; }
  bytes=count*stride; *out=(uint32_t)bytes; return 1;
}
static INLINE int xe_trace_stream_write(xe_trace_stream *s, const void *p, size_t n) {
  if (!s || !s->file || s->failed) return 0;
  if (n && xe_cache_file_write(p,1,n,s->file)!=n) s->failed=1;
  return !s->failed;
}
static INLINE void xe_trace_stream_flush(xe_trace_stream *s) {
  if (s && s->file && !s->failed && filestream_flush(s->file)) s->failed=1;
}
static INLINE int64_t xe_trace_stream_begin(xe_trace_stream *s,const void *p,size_t n) {
  int64_t position;
  if (!s || !s->file || s->failed || !p || !n) return -1;
  position=filestream_tell(s->file);
  if(position<0 || (uint64_t)n>(uint64_t)INT64_MAX-(uint64_t)position ||
      !xe_trace_stream_write(s,p,n)) { s->failed=1; return -1; }
  return position;
}
/* Patch the header and restore the actual end, never a truncated u32 position.
 * Any failed seek/write latches the stream before later records can append. */
static INLINE int xe_trace_stream_finish(xe_trace_stream *s,int64_t start,
    const void *header,size_t bytes,size_t payload) {
  int64_t end;
  if(!s || !s->file || s->failed) return 0;
  end=filestream_tell(s->file);
  if(start<0 || end<start || !header || !bytes || payload>UINT32_MAX ||
      (uint64_t)bytes>(uint64_t)(end-start) ||
      (uint64_t)(end-start)-bytes!=payload ||
      filestream_seek(s->file,start,RETRO_VFS_SEEK_POSITION_START)<0 ||
      filestream_tell(s->file)!=start ||
      !xe_trace_stream_write(s,header,bytes) ||
      filestream_seek(s->file,end,RETRO_VFS_SEEK_POSITION_START)<0 ||
      filestream_tell(s->file)!=end) {
    s->failed=1; return 0;
  }
  return 1;
}
/* Snappy can gather a block into its ALREADY allocated working input when a
 * Peek span ends. Do not concatenate these ranges into a second heap buffer. */
typedef struct xe_trace_source {
  const unsigned char *data[2];
  size_t size[2], offset, remaining;
  unsigned index;
  int failed;
} xe_trace_source;
static INLINE int xe_trace_source_init(xe_trace_source *s,const void *a,size_t an,
    const void *b,size_t bn) {
  if(!s) return 0;
  memset(s,0,sizeof(*s));
  if((an && !a) || (bn && !b) || bn>SIZE_MAX-an) { s->failed=1; return 0; }
  s->data[0]=(const unsigned char*)a; s->data[1]=(const unsigned char*)b;
  s->size[0]=an; s->size[1]=bn; s->remaining=an+bn; return 1;
}
static INLINE const char *xe_trace_source_peek(xe_trace_source *s,size_t *n) {
  if(!n) return NULL;
  *n=0;
  if(!s || s->failed) return NULL;
  while(s->index<2 && s->offset==s->size[s->index]) { ++s->index; s->offset=0; }
  if(s->index==2) return "";
  *n=s->size[s->index]-s->offset;
  return (const char*)(s->data[s->index]+s->offset);
}
static INLINE void xe_trace_source_skip(xe_trace_source *s,size_t n) {
  size_t step, available;
  if(!s || s->failed) return;
  if(n>s->remaining) { s->failed=1; s->remaining=0; return; }
  s->remaining-=n;
  while(n) {
    if(s->offset==s->size[s->index]) { ++s->index; s->offset=0; continue; }
    available=s->size[s->index]-s->offset; step=n<available?n:available;
    s->offset+=step; n-=step;
  }
}
#endif
