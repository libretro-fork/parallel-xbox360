/* Immutable cold-opened VFS fragment ownership. MSVC C89. */
#ifndef XE_CORE_SVOD_H
#define XE_CORE_SVOD_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_svod_files xe_svod_files;
/* Open all files at mount to keep allocations/handle creation out of reads.
 * Enumeration is ASCII-case-insensitive sorted like the original SVOD reader.
 * Stop/join all users before close. Mapped views borrow the owner's lifetime. */
xe_svod_files *xe_svod_files_open(const char *package, size_t expected);
void xe_svod_files_close(xe_svod_files *files);
uint64_t xe_svod_files_size(const xe_svod_files *files);
int xe_svod_files_range(const xe_svod_files *files, size_t file, uint64_t offset, size_t length);
int xe_svod_files_read(xe_svod_files *files, size_t file, uint64_t offset, void *out, size_t length);
size_t xe_svod_files_transfer(xe_svod_files *,size_t,uint64_t,void *,size_t);
/* Only immutable mappings are returned. No fallback allocation/copy. */
const uint8_t *xe_svod_files_view(const xe_svod_files *files,size_t file,uint64_t offset,size_t length);
/* Mount-parser-owned sector cache. Fallback views expire on reuse of their
 * direct-mapped slot; consume fields/name before recursion or the next get.
 * No whole-file slurp and no growing sector map. Set valid=0 before first use. */
typedef struct xe_svod_sectors {
   uint8_t bytes[4][2048];
   uint64_t offsets[4];
   size_t files[4];
   unsigned valid;
} xe_svod_sectors;
/* One cold-mount scratch owner, shared across recursion. Names are consumed
 * into the final entry path before reuse. Nothing is retained after mount. */
typedef struct xe_svod_directory_scratch {
   xe_svod_sectors sectors;
   char name_utf8[3 * 255];
} xe_svod_directory_scratch;
const uint8_t *xe_svod_sector(xe_svod_files *,xe_svod_sectors *,size_t,uint64_t);
/* Explicit CP1252->UTF8 directly into final caller storage; no temporary UTF16
 * vectors. Count first; output must not alias input. No terminator is added. */
size_t xe_svod_name_size(const uint8_t *,size_t);
int xe_svod_name_write(char *,size_t,const uint8_t *,size_t);
#ifdef __cplusplus
}
#endif
#endif
