/* Four-byte signature probes, one VFS handle, no mapping/slurp. MSVC C89. */
#ifndef XE_CORE_FILE_PROBE_H
#define XE_CORE_FILE_PROBE_H
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <retro_inline.h>
#include <streams/file_stream.h>
typedef struct xe_file_probe {
 RFILE *stream;
 int64_t size;
 unsigned char head[4];
} xe_file_probe;
static INLINE int xe_file_probe_four(RFILE *file,unsigned char *out)
{
 unsigned char bytes[4];
 unsigned done=0;
 int64_t got;
 while(done<4) {
  errno=0;got=filestream_read(file,bytes+done,4-done);
  if(got<0 && errno==EINTR) continue;
  if(got<=0 || (uint64_t)got>4-done) return 0;
  done+=(unsigned)got;
 }
 memcpy(out,bytes,4);return 1;
}
/* Zero initialize, close before reuse. Publish only a complete header. */
static INLINE int xe_file_probe_open(xe_file_probe *probe,const char *path)
{
 xe_file_probe opened;
 if(!probe || probe->stream || !path || !*path) return 0;
 memset(&opened,0,sizeof(opened));
 opened.stream=filestream_open(path,RETRO_VFS_FILE_ACCESS_READ,
       RETRO_VFS_FILE_ACCESS_HINT_NONE);
 if(!opened.stream) return 0;
 opened.size=filestream_get_size(opened.stream);
 if(opened.size<4 || !xe_file_probe_four(opened.stream,opened.head)) {
  filestream_close(opened.stream);return 0;
 }
 *probe=opened;return 1;
}
static INLINE int xe_file_probe_tail(xe_file_probe *probe,unsigned char *out)
{
 if(!probe || !probe->stream || !out || probe->size<4) return 0;
 if(probe->size==4) {memcpy(out,probe->head,4);return 1;}
 if(filestream_seek(probe->stream,probe->size-4,RETRO_VFS_SEEK_POSITION_START)<0)
  return 0;
 return xe_file_probe_four(probe->stream,out);
}
static INLINE int xe_file_probe_close(xe_file_probe *probe)
{
 int result=0;
 if(!probe) return 0;
 if(probe->stream) result=filestream_close(probe->stream);
 memset(probe,0,sizeof(*probe));return result==0;
}
#endif
