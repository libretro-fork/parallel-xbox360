#include "../../libretro/core_vfs.h"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d\n", __LINE__); return 1; } } while (0)
static struct retro_vfs_interface iface;
static struct retro_vfs_interface_info file_info, path_info;
static unsigned mode, calls, versions[2];
void filestream_vfs_init(const struct retro_vfs_interface_info *info) { file_info = *info; }
void path_vfs_init(const struct retro_vfs_interface_info *info) { path_info = *info; }
static bool RETRO_CALLCONV environment(unsigned command, void *opaque) {
   struct retro_vfs_interface_info *info = (struct retro_vfs_interface_info *)opaque;
   if (command != RETRO_ENVIRONMENT_GET_VFS_INTERFACE || calls >= 2) return false;
   versions[calls++] = info->required_interface_version;
   info->iface = &iface;
   if (mode == 1) return true;
   if (mode == 2) return info->required_interface_version == 2;
   if (mode == 4) { info->iface = NULL; return true; }
   return false; /* Deliberately leave a stale pointer on rejection. */
}
int main(void) {
   for (mode = 1; mode < 5; ++mode) {
      calls = 0;
      xe_core_vfs_init(environment);
      CHECK(versions[0] == 3 && calls == (mode == 1 ? 1u : 2u));
      CHECK(file_info.required_interface_version == (mode == 1 ? 3u : 2u));
      CHECK(path_info.required_interface_version == file_info.required_interface_version);
      CHECK(file_info.iface == (mode <= 2 ? &iface : NULL));
      CHECK(path_info.iface == file_info.iface);
      if (mode != 1) CHECK(versions[1] == 2);
   }
   calls = 0;
   xe_core_vfs_init(NULL);
   CHECK(!calls && !file_info.iface && !path_info.iface);
   puts("PASS: frontend VFS3 directories, VFS2 fallback, rejected/null interfaces and native reset");
   return 0;
}
