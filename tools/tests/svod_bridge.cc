/* Typed package/File ABI only. Scenarios and fake VFS live in the C89 driver. */
#include "xenia/vfs/devices/xcontent_container_device.h"
#include "xenia/vfs/devices/xcontent_devices/svod_container_device.h"
#include "xenia/base/string.h"
#include <cstring>
#include <cstdio>
extern "C" size_t xe_svod_native_header(void* out,size_t capacity) {
  using xe::vfs::XContentContainerHeader;
  if(capacity<sizeof(XContentContainerHeader)) return 0;
  XContentContainerHeader header{};
  std::memcpy(&header,"CON ",4);
  header.content_metadata.volume_type=xe::vfs::XContentVolumeType::kSvod;
  header.content_metadata.data_file_count=3;
  header.content_metadata.volume_descriptor.svod.features.bits.enhanced_gdf_layout=1;
  std::memcpy(out,&header,sizeof(header));return sizeof(header);
}
extern "C" void* xe_svod_native_open(const char* path) {
  auto device=xe::vfs::XContentContainerDevice::CreateContentDevice("package:",xe::to_path(std::string_view(path)));
  if(!device) {std::fprintf(stderr,"SVOD factory failed: %s\n",path);return nullptr;}
  if(!device->Initialize()) {std::fprintf(stderr,"SVOD initialize failed: %s\n",path);return nullptr;}
  return device.release();
}
extern "C" void xe_svod_native_close(void* device) {delete static_cast<xe::vfs::XContentContainerDevice*>(device);}
extern "C" int xe_svod_native_read(void* device,const char* name,size_t offset,void* out,size_t length,size_t* read) {
  auto* entry=static_cast<xe::vfs::Device*>(static_cast<xe::vfs::XContentContainerDevice*>(device))->ResolvePath(name);
  if(!entry) return 0;
  xe::vfs::File* file=nullptr;
  if(entry->Open(1,&file) || !file) return 0;
  auto status=file->ReadSync(std::span<uint8_t>(static_cast<uint8_t*>(out),length),offset,read);
  file->Destroy();return status==0;
}
