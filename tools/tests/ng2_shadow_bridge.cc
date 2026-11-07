/* Typed access to the actual bundled patch registry; checks are C89. */
#include "libretro/libretro_gamedb.h"
#include "xenia/patcher/patch_db.h"
#include <algorithm>
#include <cstring>
extern "C" int xe_ng2_shadow_query(uint32_t title,uint64_t hash,
    char* key,size_t capacity,uint32_t* words,unsigned* enabled) {
  const auto& options=gamedb::find_patches(title,hash);
  const gamedb::PatchOption* selected=nullptr;
  for (const auto& option:options) {
    if (option.label.find("Hi-res Shadows")==std::string::npos)continue;
    if (selected)return -1;
    selected=&option;
  }
  if (!selected)return 0;
  if (!key || !words || !enabled || selected->key.size()>=capacity)return -1;
  unsigned matches=0;
  for (const auto& file:xe::patcher::EnumerateBundledPatchesForTitle(title)) {
    if (std::find(file.entry.hashes.begin(),file.entry.hashes.end(),hash)==file.entry.hashes.end())continue;
    for (const auto& patch:file.entry.patch_info) {
      if (patch.patch_name!="Hi-res Shadows")continue;
      if (++matches!=1 || patch.patch_data.size()!=2)return -1;
      for (unsigned i=0;i<2;++i) {
        const auto& data=patch.patch_data[i];
        if (data.data.patch_data.size()!=4)return -1;
        const uint8_t* p=data.data.patch_data.data();
        words[i*2]=data.address;
        words[i*2+1]=(uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];
      }
    }
  }
  if (matches!=1)return -1;
  std::memcpy(key,selected->key.c_str(),selected->key.size()+1);
  *enabled=selected->default_on?1u:0u;
  return 1;
}
extern "C" int xe_ng2_shadow_forget(void) {
  gamedb::forget_patches();return gamedb::patches().empty();
}
