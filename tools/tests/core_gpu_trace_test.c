/* Borrowed trace spans, hostile VFS and actual trace protocol fixture. C89. */
#include "core_gpu_trace.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x); exit(1); } } while(0)
int xe_test_trace_destructor(const char*,uint8_t*);
int xe_test_trace_emit(const char*,uint8_t*,const uint32_t*,const uint32_t*,const void*);
size_t xe_test_trace_compress(const void*,size_t,const void*,size_t,char*);
size_t xe_test_trace_compress_flat(const char*,size_t,char*);
static unsigned char file_bytes[8192], memory[16384], snapshot[10485760];
static uint32_t gamma[256], pwl[384];
static unsigned char parts_a[131072],parts_b[131072],flat[262144];
static char compressed[306000], reference[306000];
static size_t position,length;
static int64_t file_origin;
static unsigned opens,closes,seeks,writes;
static int fault,interrupted;
static const unsigned char* borrowed;
static size_t borrowed_size, borrowed_at;
static struct retro_vfs_file_handle* RETRO_CALLCONV open_file(const char* p,unsigned m,unsigned h) {
  (void)m;(void)h;
  CHECK(!strcmp(p,"trace://capture")); if(fault==1) return NULL;
  ++opens; position=length=0; return (struct retro_vfs_file_handle*)file_bytes;
}
static int RETRO_CALLCONV close_file(struct retro_vfs_file_handle* f) { (void)f; ++closes; return fault==6?-1:0; }
static int64_t RETRO_CALLCONV size_file(struct retro_vfs_file_handle* f) { (void)f; return (int64_t)length; }
static int64_t RETRO_CALLCONV tell_file(struct retro_vfs_file_handle* f) { (void)f; return fault==7?-1:file_origin+(int64_t)position; }
static int64_t RETRO_CALLCONV seek_file(struct retro_vfs_file_handle* f,int64_t at,int origin) {
  (void)f; CHECK(origin==RETRO_VFS_SEEK_POSITION_START); ++seeks;
  if(fault==3 || (fault==8 && !(seeks%2))) return -1;
  if(at<file_origin || (uint64_t)(at-file_origin)>sizeof(file_bytes)) return -1;
  if(fault!=9) position=(size_t)(at-file_origin);
  return 0;
}
static int64_t RETRO_CALLCONV read_file(struct retro_vfs_file_handle* f,void* p,uint64_t n) {
  (void)f; if(n>length-position) n=length-position;
  memcpy(p,file_bytes+position,(size_t)n); position+=(size_t)n; return (int64_t)n;
}
static int64_t RETRO_CALLCONV write_file(struct retro_vfs_file_handle* f,const void* p,uint64_t n) {
  size_t take=n>13?13:(size_t)n; (void)f; ++writes;
  if(fault==2) { errno=ENOSPC; return -1; } if(fault==4) return 0;
  if(interrupted) { interrupted=0; errno=EINTR; return -1; }
  if(borrowed) { CHECK(p==borrowed+borrowed_at); CHECK(take<=borrowed_size-borrowed_at); borrowed_at+=take; }
  CHECK(take<=sizeof(file_bytes)-position); memcpy(file_bytes+position,p,take);
  position+=take; if(position>length) length=position; return (int64_t)take;
}
static int RETRO_CALLCONV flush_file(struct retro_vfs_file_handle* f) { (void)f; return fault==5?-1:0; }
static int32_t RETRO_CALLCONV stat_file(const char* p,int32_t* n) { (void)p;if(n)*n=0;return RETRO_VFS_STAT_IS_VALID|RETRO_VFS_STAT_IS_DIRECTORY; }
static int RETRO_CALLCONV mkdir_file(const char* p) { (void)p;return 0; }
static void stream_tests(int lifetime) {
  struct retro_vfs_interface iface;
  struct retro_vfs_interface_info info;
  xe_trace_stream stream={0};
  uint32_t header[4]={1,2,3,4};
  int64_t start;
  unsigned before,i;
  uint32_t bytes;
  memset(&iface,0,sizeof(iface)); iface.open=open_file;iface.close=close_file;
  iface.tell=tell_file;iface.size=size_file;iface.seek=seek_file;iface.read=read_file;
  iface.write=write_file;iface.flush=flush_file;iface.stat=stat_file;iface.mkdir=mkdir_file;
  info.required_interface_version=3; info.iface=&iface;
  filestream_vfs_init(&info);path_vfs_init(&info);
  CHECK(xe_trace_stream_open(&stream,"trace://capture"));
  interrupted=1; start=xe_trace_stream_begin(&stream,header,sizeof(header)); CHECK(start==0);
  borrowed=memory; borrowed_size=100; borrowed_at=0;
  CHECK(xe_trace_stream_write(&stream,borrowed,borrowed_size)); CHECK(borrowed_at==100); borrowed=NULL;
  header[3]=100; CHECK(xe_trace_stream_finish(&stream,start,header,sizeof(header),100));
  CHECK(position==116 && length==116 && !memcmp(file_bytes,header,16) && !memcmp(file_bytes+16,memory,100));
  CHECK(xe_trace_stream_close(&stream)); CHECK(xe_trace_stream_close(&stream)); CHECK(opens==closes);
  for(i=1;i<=9;++i) {
    fault=0;seeks=0; CHECK(xe_trace_stream_open(&stream,"trace://capture"));
    if(i==1) { CHECK(xe_trace_stream_close(&stream)); fault=1; CHECK(!xe_trace_stream_open(&stream,"trace://capture")); continue; }
    start=xe_trace_stream_begin(&stream,header,16); CHECK(start==0);
    CHECK(xe_trace_stream_write(&stream,memory,100)); fault=(int)i;
    if(i==2 || i==4) CHECK(!xe_trace_stream_write(&stream,memory,1));
    else if(i==5) { xe_trace_stream_flush(&stream); CHECK(stream.failed); }
    else if(i==6) { CHECK(!xe_trace_stream_close(&stream)); fault=0; continue; }
    else CHECK(!xe_trace_stream_finish(&stream,start,header,16,100));
    before=writes; CHECK(!xe_trace_stream_write(&stream,memory,1)); CHECK(before==writes);
    CHECK(!xe_trace_stream_close(&stream)); fault=0;
  }
  CHECK(opens==closes);
  if(lifetime) {
    CHECK(xe_test_trace_destructor("trace://capture",memory)); CHECK(opens==closes);
    fault=2; CHECK(!xe_test_trace_destructor("trace://capture",memory)); fault=0;
    CHECK(opens==closes);
  }
  CHECK(xe_trace_stream_open(&stream,"trace://capture")); file_origin=INT64_C(1)<<33;
  start=xe_trace_stream_begin(&stream,header,16); CHECK(start==file_origin);
  CHECK(xe_trace_stream_write(&stream,memory,100));
  CHECK(xe_trace_stream_finish(&stream,start,header,16,100));
  CHECK(filestream_tell(stream.file)==file_origin+116);
  CHECK(xe_trace_stream_close(&stream)); file_origin=0;
  CHECK(xe_trace_stream_open(&stream,"trace://capture")); file_origin=INT64_MAX-8;
  before=writes; CHECK(xe_trace_stream_begin(&stream,header,16)<0 && before==writes);
  CHECK(!xe_trace_stream_close(&stream)); file_origin=0;
  stream.failed=0; bytes=0x12345678;
  CHECK(!xe_trace_payload_size(&stream,memory,UINT64_MAX,4,UINT64_MAX,&bytes));
  CHECK(bytes==0x12345678 && stream.failed); stream.failed=0;
  CHECK(!xe_trace_payload_size(&stream,NULL,1,1,UINT32_MAX,&bytes)); stream.failed=0;
  CHECK(!xe_trace_payload_size(&stream,memory,17,1,16,&bytes)); stream.failed=0;
  CHECK(xe_trace_payload_size(&stream,memory,25,4,100,&bytes) && bytes==100);
  CHECK(xe_trace_stream_open(&stream,"trace://capture")); start=xe_trace_stream_begin(&stream,header,16);
  CHECK(!xe_trace_stream_finish(&stream,start,header,16,SIZE_MAX)); CHECK(stream.failed); CHECK(!xe_trace_stream_close(&stream));
  CHECK(xe_trace_stream_open(&stream,"trace://capture")); start=xe_trace_stream_begin(&stream,header,16);
  CHECK(!xe_trace_stream_finish(&stream,start,header,16,1)); CHECK(!xe_trace_stream_close(&stream));
  info.iface=NULL; filestream_vfs_init(&info); path_vfs_init(&info);
  puts("PASS: borrowed short/EINTR writes, exact header patch/end, failure latch, flush/seek/close faults, double-close");
}
static void source_tests(void) {
  xe_trace_source source;
  const char* p;
  size_t n,an,bn,size_a,size_b;
  unsigned i,j;
  uint32_t state=0x4567abcd;
  for(i=0;i<sizeof(parts_a);++i) {
    state^=state<<13;state^=state>>17;state^=state<<5;
    parts_a[i]=(unsigned char)state; parts_b[i]=(unsigned char)(i%7);
  }
  CHECK(!xe_trace_source_init(&source,NULL,1,NULL,0));
  CHECK(!xe_trace_source_init(&source,parts_a,SIZE_MAX,parts_b,1));
  CHECK(xe_trace_source_init(&source,NULL,0,NULL,0)); CHECK(xe_trace_source_peek(&source,&n) && !n);
  for(i=0;i<80;++i) {
    an=i<8?i:(i*7919u)%sizeof(parts_a); bn=(i*5939u)%sizeof(parts_b);
    if(i==8) { an=65535;bn=65537; } if(i==9) { an=65536;bn=65536; }
    CHECK(xe_trace_source_init(&source,parts_a,an,parts_b,bn));
    for(j=0;j<an+bn;++j) {
      p=xe_trace_source_peek(&source,&n); CHECK(p && n && source.remaining==an+bn-j);
      CHECK((const unsigned char*)p==(j<an?parts_a+j:parts_b+j-an));
      xe_trace_source_skip(&source,1);
    }
    CHECK(!source.failed && !source.remaining); xe_trace_source_skip(&source,1); CHECK(source.failed);
    memcpy(flat,parts_a,an);memcpy(flat+an,parts_b,bn);
    size_a=xe_test_trace_compress(parts_a,an,parts_b,bn,compressed);
    size_b=xe_test_trace_compress_flat((const char*)flat,an+bn,reference);
    CHECK(size_a==size_b && !memcmp(compressed,reference,size_a));
  }
  puts("PASS: 80 borrowed Snappy streams byte-identical to concatenation, empty spans and 64KiB boundaries");
}
int main(int argc,char** argv) {
  unsigned i;
  CHECK(argc==2 || (argc==3 && !strcmp(argv[2],"legacy")));
  for(i=0;i<sizeof(memory);++i) memory[i]=(unsigned char)(i*37);
  for(i=0;i<sizeof(snapshot);++i) snapshot[i]=(unsigned char)(i%37);
  for(i=0;i<256;++i) gamma[i]=i*UINT32_C(0x00040101);
  for(i=0;i<384;++i) pwl[i]=i*UINT32_C(0x00030011);
  stream_tests(argc==2); source_tests();
  CHECK(xe_test_trace_emit(argv[1],memory,gamma,pwl,snapshot));
  puts("PASS: actual trace writer fixture with all command types, cached suppression, gamma and 10MiB EDRAM");
  return 0;
}
