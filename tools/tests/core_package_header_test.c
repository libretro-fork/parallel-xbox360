/* Adversarial frontend VFS, also used by the actual container ABI bridge. */
#include "libretro/core_package_header.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"package line %u\n",(unsigned)__LINE__); exit(1); } } while (0)
#define CAPACITY 131072
static unsigned char data[CAPACITY], output[CAPACITY];
static const char path[] = "content://package/\xE2\x98\x83";
static size_t header_size = 1024, volume_offset = 0x3A9;
static int64_t length, position;
static unsigned opens, closes, reads, seeks, transferred, limit;
static int fault;
#ifdef XE_PACKAGE_NATIVE
size_t xe_package_native_size(void);
size_t xe_package_native_volume_offset(void);
int xe_package_native_read(const char *, const void *, size_t);
int xe_package_native_type(const char *);
int xe_package_native_initialize(const char *, const void *, size_t);
#endif
static struct retro_vfs_file_handle *RETRO_CALLCONV open_file(const char *name, unsigned mode, unsigned hints)
{
   CHECK(!strcmp(name, path) && mode == RETRO_VFS_FILE_ACCESS_READ && !hints);
   ++opens;
   if (fault == 1) return NULL;
   position = 0;
   return (struct retro_vfs_file_handle *)data;
}
static int RETRO_CALLCONV close_file(struct retro_vfs_file_handle *file)
{ (void)file; ++closes; return fault == 8 ? -1 : 0; }
static int64_t RETRO_CALLCONV size_file(struct retro_vfs_file_handle *file)
{ (void)file; return fault == 2 ? -1 : length; }
static int64_t RETRO_CALLCONV tell_file(struct retro_vfs_file_handle *file)
{ (void)file; return position; }
static int64_t RETRO_CALLCONV seek_file(struct retro_vfs_file_handle *file, int64_t at, int origin)
{
   (void)file; ++seeks;
   if (fault == 5 || origin != RETRO_VFS_SEEK_POSITION_START || at < 0 || at > length) return -1;
   position = at;
   return fault == 10 ? at + 1 : at;
}
static int64_t RETRO_CALLCONV read_file(struct retro_vfs_file_handle *file, void *out, uint64_t size)
{
   size_t count;
   (void)file; ++reads;
   CHECK(size && size <= header_size && position >= 0 && (uint64_t)position <= header_size);
   if (fault == 3) return -1;
   if (fault == 4) return 0;
   if (fault == 6) { fault = 0; errno = EINTR; return -1; }
   if (fault == 7) return (int64_t)size + 1;
   if (fault == 9 && position >= 5) return 0;
   count = (size_t)size;
   if (count > limit) count = limit;
   if ((uint64_t)count > (uint64_t)(length - position)) count = (size_t)(length - position);
   CHECK(count <= header_size - (size_t)position);
   memcpy(out, data + (size_t)position, count);
   position += count; transferred += (unsigned)count;
   return (int64_t)count;
}
static void setup(unsigned volume)
{
   size_t i;
   for (i = 0; i < header_size; ++i) data[i] = (unsigned char)(i * 17 + 3);
   memcpy(data, "LIVE", 4);
   data[volume_offset] = (unsigned char)(volume >> 24);
   data[volume_offset + 1] = (unsigned char)(volume >> 16);
   data[volume_offset + 2] = (unsigned char)(volume >> 8);
   data[volume_offset + 3] = (unsigned char)volume;
   length = INT64_MAX; position = 0;
   opens = closes = reads = seeks = transferred = 0;
   limit = (unsigned)header_size; fault = 0;
   memset(output, 0xA5, sizeof(output));
}
int main(void)
{
   struct retro_vfs_interface iface;
   struct retro_vfs_interface_info info;
   unsigned volume, i, chunk;
   uint64_t size;
   static const char signatures[3][5] = {"CON ", "LIVE", "PIRS"};
#ifdef XE_PACKAGE_NATIVE
   header_size = xe_package_native_size();
   volume_offset = xe_package_native_volume_offset();
   CHECK(volume_offset == 0x3A9);
   printf("native header=%u bytes, volume offset=%u\n", (unsigned)header_size, (unsigned)volume_offset);
#endif
   CHECK(header_size <= CAPACITY && volume_offset + 4 <= header_size);
   memset(&iface, 0, sizeof(iface));
   iface.open = open_file; iface.close = close_file; iface.size = size_file;
   iface.tell = tell_file; iface.seek = seek_file; iface.read = read_file;
   info.required_interface_version = 2; info.iface = &iface; filestream_vfs_init(&info);
   for (i = 0; i < 3; ++i) {
      setup(1); memcpy(data, signatures[i], 4); volume = 99;
      CHECK(xe_package_probe_type(path, header_size, volume_offset, &volume) == 1);
      CHECK(volume == 1 && closes == 1 && transferred == 8);
   }
   for (i = 0; i < 3; ++i) {
      setup(i == 2 ? 0x10000u : i); volume = 99;
      CHECK(xe_package_probe_type(path, header_size, volume_offset, &volume) == (i == 2 ? 0 : 1));
      CHECK(opens == 1 && closes == 1 && transferred == 8 && seeks == 1);
      CHECK(volume == (i == 2 ? 99u : i));
#ifdef XE_PACKAGE_NATIVE
      setup(i); CHECK(xe_package_native_type(path) == (i == 2 ? -1 : (int)i));
      CHECK(opens == 1 && closes == 1 && transferred == 8);
#endif
   }
   for (chunk = 1; chunk <= 7; ++chunk) {
      setup(1); limit = chunk;
      CHECK(xe_package_read_header(path, output, header_size, &size) == 1);
      CHECK(size == INT64_MAX && opens == 1 && closes == 1 && transferred == header_size);
      CHECK(!memcmp(output, data, header_size));
      setup(1); limit = chunk; volume = 99;
      CHECK(xe_package_probe_type(path, header_size, volume_offset, &volume) == 1 && volume == 1);
      CHECK(opens == 1 && closes == 1 && transferred == 8);
   }
   setup(0); length = (int64_t)header_size - 1; size = 17;
   CHECK(!xe_package_read_header(path, output, header_size, &size));
   CHECK(size == 17 && !reads && closes == 1 && output[0] == 0xA5);
   setup(0); length = 0; volume = 99;
   CHECK(!xe_package_probe_type(path, header_size, volume_offset, &volume));
   CHECK(!reads && closes == 1 && volume == 99);
   setup(0); memcpy(data, "junk", 4); volume = 99;
   CHECK(!xe_package_probe_type(path, header_size, volume_offset, &volume));
   CHECK(transferred == 4 && !seeks && closes == 1 && volume == 99);
   for (i = 1; i <= 10; ++i) {
      if (i == 6) continue;
      setup(0); fault = (int)i; limit = 5; size = 17;
      if (i != 5 && i != 10) {
         CHECK(xe_package_read_header(path, output, header_size, &size) == -1);
         CHECK(size == 17 && opens == 1 && closes == (i == 1 ? 0u : 1u));
      }
      setup(0); fault = (int)i; volume = 99;
      if (i != 9) {
         CHECK(xe_package_probe_type(path, header_size, volume_offset, &volume) == -1);
         CHECK(volume == 99 && opens == 1 && closes == (i == 1 ? 0u : 1u));
      }
#ifdef XE_PACKAGE_NATIVE
      setup(0); fault = (int)i;
      CHECK(xe_package_native_type(path) == -1);
      CHECK(opens == 1 && closes == (i == 1 ? 0u : 1u));
      setup(0); fault = (int)i; limit = 5;
      if (i != 5 && i != 10) {
         CHECK(!xe_package_native_read(path, data, header_size));
         CHECK(opens == 1 && closes == (i == 1 ? 0u : 1u));
         setup(0); fault = (int)i; limit = 5;
         CHECK(!xe_package_native_initialize(path, data, header_size));
         CHECK(opens == 1 && closes == (i == 1 ? 0u : 1u));
      }
#endif
   }
   setup(0); fault = 6; size = 17;
   CHECK(xe_package_read_header(path, output, header_size, &size) == 1 && closes == 1);
   setup(0); fault = 6; volume = 99;
   CHECK(xe_package_probe_type(path, header_size, volume_offset, &volume) == 1 && closes == 1);
#ifdef XE_PACKAGE_NATIVE
   setup(1); CHECK(xe_package_native_read(path, data, header_size));
   CHECK(opens == 1 && closes == 1 && transferred == header_size);
   setup(1); CHECK(xe_package_native_initialize(path, data, header_size));
   CHECK(opens == 1 && closes == 1 && transferred == header_size);
   setup(0); memcpy(data, "junk", 4);
   CHECK(!xe_package_native_initialize(path, data, header_size));
   CHECK(opens == 1 && closes == 1 && transferred == header_size);
#endif
   setup(0);
   CHECK(xe_package_probe_type(path, 3, 0, &volume) == -1);
   CHECK(xe_package_probe_type(path, header_size, header_size, &volume) == -1);
   CHECK(xe_package_read_header(NULL, output, header_size, &size) == -1 && !opens);
   puts("PASS package VFS probes, direct headers, 64-bit sizes, short I/O, faults and exact-once closure");
   return 0;
}
