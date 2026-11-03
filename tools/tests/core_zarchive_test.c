/* Real ZArchive blocks/cache regression; MSVC C89. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <streams/file_stream.h>
#include <lrc_hash.h>
/* Independent footer-format check: the stored digest covers the complete
 * archive with the footer's 32 digest octets replaced by zero. */
static int verify_footer(const char *path) {
  RFILE *file = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ, 0);
  struct sha256_state state;
  uint8_t buffer[4096], footer_bytes[144], digest[32];
  int64_t size, left, got;
  if (!file) return 0;
  size = filestream_get_size(file);
  if (size < 144) { filestream_close(file); return 0; }
  sha256_stream_init(&state, 0);
  left = size - 144;
  while (left) {
    size_t want = left > (int64_t)sizeof(buffer) ? sizeof(buffer) : (size_t)left;
    got = filestream_read(file, buffer, want);
    if (got <= 0 || got > (int64_t)want) { filestream_close(file); return 0; }
    sha256_stream_update(&state, buffer, (size_t)got); left -= got;
  }
  left = 0;
  while (left < 144) {
    got = filestream_read(file, footer_bytes + (size_t)left, (size_t)(144 - left));
    if (got <= 0 || got > 144 - left) { filestream_close(file); return 0; }
    left += got;
  }
  filestream_close(file);
  memcpy(digest, footer_bytes + 96, 32); memset(footer_bytes + 96, 0, 32);
  sha256_stream_update(&state, footer_bytes, sizeof(footer_bytes));
  sha256_stream_final(&state, buffer);
  return memcmp(digest, buffer, 32) == 0;
}
static struct retro_vfs_file_handle *RETRO_CALLCONV vfs_open(const char *path, unsigned mode, unsigned hints) {
  return (struct retro_vfs_file_handle *)retro_vfs_file_open_impl(path, mode, hints);
}
static int RETRO_CALLCONV vfs_close(struct retro_vfs_file_handle *file) {
  return retro_vfs_file_close_impl((libretro_vfs_implementation_file *)file);
}
static int64_t RETRO_CALLCONV vfs_size(struct retro_vfs_file_handle *file) {
  return retro_vfs_file_size_impl((libretro_vfs_implementation_file *)file);
}
static int64_t RETRO_CALLCONV vfs_seek(struct retro_vfs_file_handle *file, int64_t offset, int origin) {
  return retro_vfs_file_seek_impl((libretro_vfs_implementation_file *)file, offset, origin);
}
static int64_t RETRO_CALLCONV vfs_read(struct retro_vfs_file_handle *file, void *out, uint64_t size) {
  if (size > 65521) size = 65521;
  return retro_vfs_file_read_impl((libretro_vfs_implementation_file *)file, out, size);
}
void *archive_test_writer(FILE *file);
int archive_test_write(void *writer,const char *name,const void *data,size_t size);
int archive_test_mkdir(void *writer,const char *name);
void archive_test_finish(void *writer);
void *archive_test_open(const char *path);
uint64_t archive_test_read(void *reader,const char *name,uint64_t offset,size_t size,void *output);
void archive_test_close(void *reader);
static unsigned char source[5*1024*1024+11], random_bytes[131079], output[70003];
int main(int argc,char **argv){
  FILE *file;
  void *writer,*reader;
  uint32_t rng=0x12345678;
  size_t i,offset,want;
  char long_name[201];
  struct retro_vfs_interface iface;
  struct retro_vfs_interface_info info;
  if(argc!=2 && argc!=3)return 2;
  memset(&iface,0,sizeof(iface));
  iface.open=vfs_open;iface.close=vfs_close;iface.size=vfs_size;
  iface.seek=vfs_seek;iface.read=vfs_read;
  info.required_interface_version=2;info.iface=argc==3?&iface:NULL;
  filestream_vfs_init(&info);
  memset(long_name,'a',sizeof(long_name)-1);long_name[200]=0;
  for(i=0;i<sizeof(source);++i)source[i]=(unsigned char)((i%251)^(i/32768));
  for(i=0;i<sizeof(random_bytes);++i){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;random_bytes[i]=(unsigned char)rng;}
  file=fopen(argv[1],"wb");if(!file)return 3;
  writer=archive_test_writer(file);if(!writer)return 4;
  if(!archive_test_mkdir(writer,"alpha"))return 11;
  if(!archive_test_write(writer,"alpha/data.bin",source,sizeof(source)) ||
     !archive_test_write(writer,"random.bin",random_bytes,sizeof(random_bytes)) ||
     !archive_test_write(writer,"empty.bin",source,0) || !archive_test_write(writer,long_name,source,11))return 5;
  archive_test_finish(writer);if(ferror(file)||fclose(file))return 6;
  if (!verify_footer(argv[1])) return 14;
  reader=archive_test_open(argv[1]);if(!reader)return 7;
  /* Reverse traversal exceeds the four-MiB cache and crosses block edges. */
  for(i=0;i<120;++i){offset=(sizeof(source)-1-(i*65521u)%sizeof(source));want=sizeof(source)-offset;if(want>sizeof(output))want=sizeof(output);
    if(archive_test_read(reader,"alpha/data.bin",offset,want,output)!=want||memcmp(output,source+offset,want))return 8;
  }
  for(offset=0;offset<sizeof(random_bytes);offset+=32761){want=sizeof(random_bytes)-offset;if(want>sizeof(output))want=sizeof(output);
    if(archive_test_read(reader,"random.bin",offset,want,output)!=want||memcmp(output,random_bytes+offset,want))return 9;
  }
  if(archive_test_read(reader,"alpha/data.bin",0,11,output)!=11||memcmp(output,source,11)||archive_test_read(reader,"random.bin",0,11,output)!=11||memcmp(output,random_bytes,11))return 13;
  if(archive_test_read(reader,"empty.bin",0,1,output)!=0||archive_test_read(reader,"absent",0,1,output)!=0)return 10;
  if(archive_test_read(reader,long_name,0,11,output)!=11||memcmp(output,source,11))return 12;
  archive_test_close(reader);puts("PASS: real archive compressed/raw blocks, cross-block random reads, cache eviction and empty files");return 0;
}
