/* Native common VFS fault injection and bounded mapped slots; MSVC C89. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <vfs/vfs_implementation.h>
static int fault, interrupted;
static unsigned opens, truncates;
static libretro_vfs_implementation_file *test_open(const char *p, unsigned mode, unsigned hints) {
  ++opens;
  if (fault == 1) return NULL;
  return retro_vfs_file_open_impl(p, mode, hints);
}
static int test_close(libretro_vfs_implementation_file *f) {
  int r = retro_vfs_file_close_impl(f); return fault == 5 ? -1 : r;
}
static int64_t test_size(libretro_vfs_implementation_file *f) {
  return fault == 2 ? -1 : retro_vfs_file_size_impl(f);
}
static int64_t test_read(libretro_vfs_implementation_file *f, void *p, uint64_t n) {
  if (fault == 3) { errno = EIO; return -1; }
  if (fault == 6) return 0;
  if (fault == 7) return (int64_t)n + 1;
  if (fault == 8) {
    if (!interrupted) { interrupted = 1; errno = EINTR; return -1; }
    if (n > 1) n = 1;
  }
  return retro_vfs_file_read_impl(f, p, n);
}
static int64_t test_truncate(libretro_vfs_implementation_file *f, int64_t n) {
  ++truncates; return fault == 4 ? -1 : retro_vfs_file_truncate_impl(f, n);
}
#define XE_IC_OPEN test_open
#define XE_IC_CLOSE test_close
#define XE_IC_SIZE test_size
#define XE_IC_READ test_read
#define XE_IC_TRUNCATE test_truncate
#define XE_IC_STAT retro_vfs_stat_impl
#define XE_IC_MKDIR retro_vfs_mkdir_impl
#include "core_instruction_cache.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"instruction cache line %d: %s\n",__LINE__,#x); return 1; } } while (0)
#ifdef XE_IC_BRIDGE
extern int xe_test_ic_mapping(const char *, size_t);
#endif
int main(int argc, char **argv) {
  union { uint32_t align; uint8_t bytes[264]; } map;
  char path[4096], invalid[41];
  const char *hash="0123456789abcdef0123456789abcdef01234567";
  libretro_vfs_implementation_file *file;
  size_t size;
  uint32_t offset=19, version=4, actual;
  unsigned before, before_open, i;
  int fresh, f;
  CHECK(argc==2);
  CHECK(xe_ic_size(0x1000,0x1008,&size) && size==264);
  CHECK(xe_ic_size(0x1000,0x1005,&size) && size==264);
  CHECK(!xe_ic_size(0,0,&size) && !xe_ic_size(8,4,&size));
#if SIZE_MAX > UINT32_MAX
  CHECK(xe_ic_size(0,UINT32_MAX,&size) && (uint64_t)size==(uint64_t)UINT32_MAX+257);
#else
  CHECK(!xe_ic_size(0,UINT32_MAX,&size));
#endif
  CHECK(xe_ic_address(0x1000,0x1008,0x1007,&offset) && offset==7);
  CHECK(!xe_ic_address(0x1000,0x1008,0x1008,&offset) && offset==7);
  CHECK(!xe_ic_address(0x1000,0x1008,0xfff,&offset));
  CHECK(xe_ic_slot(map.bytes,264,0)==map.bytes+256 &&
      xe_ic_slot(map.bytes,264,3)==map.bytes+256 &&
      xe_ic_slot(map.bytes,264,7)==map.bytes+260);
  CHECK(!xe_ic_slot(map.bytes,264,8) && !xe_ic_slot(map.bytes,264,UINT32_MAX) &&
      !xe_ic_slot(map.bytes,259,0) && !xe_ic_slot(map.bytes+1,263,0));
  memset(map.bytes,0x5d,sizeof(map.bytes));
  CHECK(xe_ic_initialize(map.bytes,264,version));
  memcpy(&actual,map.bytes,4); CHECK(actual==version);
  for(i=4;i<256;++i) CHECK(!map.bytes[i]);
  for(i=256;i<264;++i) CHECK(map.bytes[i]==0x5d);
  memset(map.bytes+256,0,8);
  CHECK(!xe_ic_initialize(map.bytes+1,263,4));
  CHECK(xe_ic_path(argv[1],hash,path,sizeof(path)));
  memcpy(invalid,hash,41); invalid[9]='/';
  CHECK(!xe_ic_path(argv[1],invalid,path,sizeof(path)));
  CHECK(!xe_ic_path(argv[1],hash,path,10));
  CHECK(xe_ic_path(argv[1],hash,path,sizeof(path)));
  retro_vfs_file_remove_impl(path);
  CHECK(xe_ic_prepare(path,264,version,&fresh) && fresh);
  file=retro_vfs_file_open_impl(path,RETRO_VFS_FILE_ACCESS_READ_WRITE | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING,0);
  CHECK(file && retro_vfs_file_size_impl(file)==264);
  map.bytes[260]=0x57;
  CHECK(retro_vfs_file_write_impl(file,map.bytes,264)==264 && !retro_vfs_file_close_impl(file));
  before=truncates;
  CHECK(xe_ic_prepare(path,264,version,&fresh) && !fresh && truncates==before);
  CHECK(xe_ic_prepare(path,264,version+1,&fresh) && fresh && truncates==before+2);
  file=retro_vfs_file_open_impl(path,RETRO_VFS_FILE_ACCESS_READ_WRITE | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING,0);
  CHECK(file && retro_vfs_file_seek_impl(file,260,RETRO_VFS_SEEK_POSITION_START)>=0 && retro_vfs_file_tell_impl(file)==260);
  actual=~0U; CHECK(retro_vfs_file_read_impl(file,&actual,4)==4 && !actual);
  CHECK(retro_vfs_file_seek_impl(file,0,RETRO_VFS_SEEK_POSITION_START)==0);
  CHECK(retro_vfs_file_write_impl(file,map.bytes,264)==264 && !retro_vfs_file_close_impl(file));
  /* Read errors, including a bogus oversized transfer, cannot authorize repair. */
  for(f=1;f<=8;++f) {
    fault=f; fresh=99; before=truncates; before_open=opens;
    if(f==8) CHECK(xe_ic_prepare(path,264,version,&fresh) && !fresh && interrupted);
    else CHECK(!xe_ic_prepare(path,f==4 ? 268 : 264,version,&fresh) && !fresh);
    if(f==1) CHECK(opens==before_open+1);
    if(f!=4) CHECK(truncates==before);
  }
  fault=0;
  CHECK(xe_ic_prepare(path,260,version,&fresh) && fresh);
  CHECK(xe_ic_prepare(path,264,version,&fresh) && fresh);
  file=retro_vfs_file_open_impl(path,RETRO_VFS_FILE_ACCESS_READ_WRITE | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING,0);
  CHECK(file && retro_vfs_file_write_impl(file,map.bytes,264)==264 && !retro_vfs_file_close_impl(file));
  before_open=opens;
  CHECK(!xe_ic_prepare(path,259,version,&fresh) && opens==before_open);
#ifdef XE_IC_BRIDGE
  CHECK(xe_test_ic_mapping(path,264));
#endif
  file=retro_vfs_file_open_impl(path,RETRO_VFS_FILE_ACCESS_READ,0);
  CHECK(file && retro_vfs_file_read_impl(file,&actual,4)==4 && actual==version);
  CHECK(!retro_vfs_file_close_impl(file));
  CHECK(!retro_vfs_file_remove_impl(path));
  puts("Instruction cache: mapped slot bounds, checked native VFS preparation and hostile I/O pass");
  return 0;
}
