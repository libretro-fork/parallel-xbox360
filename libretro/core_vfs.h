/* Negotiate directory operations with the same frontend as file streams.
 * Older file-only VFS frontends retain the previous native path fallback. */
#ifndef XE_CORE_VFS_H
#define XE_CORE_VFS_H
#include <retro_inline.h>
#include <streams/file_stream.h>
#include <file/file_path.h>
static INLINE void xe_core_vfs_init(retro_environment_t environment)
{
   struct retro_vfs_interface_info info;
   info.iface = NULL;
   info.required_interface_version = PATH_REQUIRED_VFS_VERSION;
   if (!environment || !environment(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &info) ||
       !info.iface) {
      info.iface = NULL;
      info.required_interface_version = FILESTREAM_REQUIRED_VFS_VERSION;
      if (!environment || !environment(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &info))
         info.iface = NULL;
   }
   filestream_vfs_init(&info);
   path_vfs_init(&info);
}
#endif
