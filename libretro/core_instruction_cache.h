/* Writable instruction flags stay mapped; never slurp them into a copy. */
#ifndef XE_CORE_INSTRUCTION_CACHE_H
#define XE_CORE_INSTRUCTION_CACHE_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <retro_inline.h>
#include <vfs/vfs_implementation.h>
#ifndef XE_IC_OPEN
#define XE_IC_OPEN retro_vfs_file_open_impl
#define XE_IC_CLOSE retro_vfs_file_close_impl
#define XE_IC_SIZE retro_vfs_file_size_impl
#define XE_IC_READ retro_vfs_file_read_impl
#define XE_IC_TRUNCATE retro_vfs_file_truncate_impl
#define XE_IC_STAT retro_vfs_stat_impl
#define XE_IC_MKDIR retro_vfs_mkdir_impl
#endif
#define XE_IC_HEADER_SIZE 256
static INLINE int xe_ic_size(uint32_t low, uint32_t high, size_t *size) {
  uint64_t length;
  if (!size || high <= low) return 0;
  length = (((uint64_t)high - low + 3) & ~(uint64_t)3) + XE_IC_HEADER_SIZE;
  if (length > (size_t)-1) return 0;
  *size = (size_t)length; return 1;
}
static INLINE int xe_ic_address(uint32_t low, uint32_t high, uint32_t address,
    uint32_t *offset) {
  if (!offset || address < low || address >= high) return 0;
  *offset = address - low; return 1;
}
/* Preserve byte-offset-to-instruction rounding, but require a complete mapped
 * word and alignment suitable for retro_atomic at the returned address. */
static INLINE uint8_t *xe_ic_slot(uint8_t *data, size_t length, uint32_t offset) {
  size_t at = (size_t)(offset & ~3U);
  if (!data || ((uintptr_t)data & 3) || length < XE_IC_HEADER_SIZE + 4 ||
      at > length - XE_IC_HEADER_SIZE - 4) return NULL;
  return data + XE_IC_HEADER_SIZE + at;
}
static INLINE int xe_ic_directory(const char *path) {
  int flags = XE_IC_STAT(path, NULL);
  if (flags & RETRO_VFS_STAT_IS_VALID) return (flags & RETRO_VFS_STAT_IS_DIRECTORY) != 0;
  if (XE_IC_MKDIR(path) == 0) return 1;
  flags = XE_IC_STAT(path, NULL);
  return (flags & RETRO_VFS_STAT_IS_VALID) && (flags & RETRO_VFS_STAT_IS_DIRECTORY);
}
/* The frontend already creates cache_root. Native common VFS is deliberate:
 * MappedMemory::Open maps a native pathname, not an arbitrary frontend VFS file.
 * Keep directory preparation and the mapping on the same backing storage. */
static INLINE int xe_ic_path(const char *root, const char *hash, char *path, size_t capacity) {
  static const char suffix[] = "/executable_addr_flags.bin";
  size_t length, i, at;
  if (!root || !*root || !hash || !path) return 0;
  length = strlen(root);
  if (strlen(hash) != 40 || capacity < 1 || length > capacity - 1 ||
      capacity - 1 - length < 8 + 1 + 40 + sizeof(suffix) - 1) return 0;
  for (i = 0; i < 40; ++i)
    if (!((hash[i] >= '0' && hash[i] <= '9') || (hash[i] >= 'a' && hash[i] <= 'f'))) return 0;
  memcpy(path, root, length); at = length;
  if (path[at - 1] != '/' && path[at - 1] != '\\') path[at++] = '/';
  memcpy(path + at, "modules", 8); at += 7;
  if (!xe_ic_directory(path)) return 0;
  path[at++] = '/'; memcpy(path + at, hash, 41); at += 40;
  if (!xe_ic_directory(path)) return 0;
  memcpy(path + at, suffix, sizeof(suffix)); return 1;
}
/* Prepare before exposing any writable mapping. Existing read failures are not
 * permission to erase flags. Only the native implementation is used: its
 * ftruncate/_chsize[_s] extension is zero-filled on the supported hosts. An
 * arbitrary frontend VFS truncate has no such padding guarantee. */
static INLINE int xe_ic_prepare(const char *path, size_t size, uint32_t version, int *fresh) {
  libretro_vfs_implementation_file *file;
  int64_t length, got;
  size_t done = 0;
  uint32_t stored = 0;
  int ok = 1, reset;
  if (fresh) *fresh = 0;
  if (!path || !*path || !fresh || size < XE_IC_HEADER_SIZE + 4 || (uint64_t)size > INT64_MAX) return 0;
  file = XE_IC_OPEN(path, RETRO_VFS_FILE_ACCESS_READ_WRITE | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING, 0);
  if (!file && !(XE_IC_STAT(path, NULL) & RETRO_VFS_STAT_IS_VALID))
    file = XE_IC_OPEN(path, RETRO_VFS_FILE_ACCESS_READ_WRITE, 0);
  if (!file) return 0;
  length = XE_IC_SIZE(file); reset = length != (int64_t)size;
  if (length < 0) ok = 0;
  if (ok && length >= 4) {
    while (done < 4) {
      errno = 0; got = XE_IC_READ(file, (uint8_t *)&stored + done, 4 - done);
      if (got < 0 && errno == EINTR) continue;
      if (got <= 0 || (uint64_t)got > 4 - done) { ok = 0; break; }
      done += (size_t)got;
    }
  } else reset = 1;
  reset = reset || stored != version;
  if (ok && reset) {
    if (length && XE_IC_TRUNCATE(file, 0) < 0) ok = 0;
    if (ok && XE_IC_TRUNCATE(file, (int64_t)size) < 0) ok = 0;
  }
  if (XE_IC_CLOSE(file) != 0) ok = 0;
  if (ok) *fresh = reset;
  return ok;
}
/* Publish after successful fresh preparation and mapping, before any borrower.
 * Flags already have native zero backing: touch only the fixed header, avoiding
 * a full-map memset and eager residency of every flag page. */
static INLINE int xe_ic_initialize(uint8_t *data, size_t length, uint32_t version) {
  if (!data || ((uintptr_t)data & 3) || length < XE_IC_HEADER_SIZE + 4) return 0;
  memset(data, 0, XE_IC_HEADER_SIZE); memcpy(data, &version, sizeof(version)); return 1;
}
#endif
