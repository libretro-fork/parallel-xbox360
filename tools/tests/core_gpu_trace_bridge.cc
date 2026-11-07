/* Existing native trace/Snappy ABI only. Fixtures and checks live in C89. */
#include "xenia/gpu/trace_writer.h"
#include "xenia/gpu/trace_io_adapter.h"
#include "third_party/snappy/snappy.h"
#include "xenia/gpu/xenos.h"
extern "C" int xe_test_trace_emit(const char* path,uint8_t* memory,
    const uint32_t* gamma,const uint32_t* pwl,const void* snapshot) {
  xe::gpu::TraceWriter writer(memory);
  if(!writer.Open(path,0x12345678)) return 0;
  writer.WritePrimaryBufferStart(16,4); writer.WritePrimaryBufferEnd();
  writer.WriteIndirectBufferStart(32,8); writer.WriteIndirectBufferEnd();
  writer.WritePacketStart(48,4); writer.WritePacketEnd();
  writer.WriteMemoryRead(0,128); writer.WriteMemoryWrite(256,4096);
  writer.WriteMemoryReadCached(8192,64); writer.WriteMemoryReadCached(8192,64);
  writer.WriteMemoryReadCachedNop(9216,32); writer.WriteMemoryReadCached(9216,32);
  writer.WriteRegisters(4,gamma,256,true);
  writer.WriteGammaRamp(reinterpret_cast<const xe::gpu::reg::DC_LUT_30_COLOR*>(gamma),
      reinterpret_cast<const xe::gpu::reg::DC_LUT_PWL_DATA*>(pwl),2);
  writer.WriteEvent(xe::gpu::EventCommand::Type::kSwap);
  writer.WriteEdramSnapshot(snapshot); writer.Flush();
  int ok=writer.is_open(); writer.Close(); return ok;
}
extern "C" int xe_test_trace_destructor(const char* path,uint8_t* memory) {
  xe::gpu::TraceWriter writer(memory);
  int ok=writer.Open(path,1);
#ifdef XE_TEST_TRACE_LEGACY
  // The archived writer's default destructor leaked its FILE; protocol-only baseline.
  writer.Close();
#endif
  return ok;
}
extern "C" size_t xe_test_trace_compress(const void* a,size_t an,const void* b,
    size_t bn,char* output) {
  xe::gpu::TraceSource source(a,an,b,bn); snappy::UncheckedByteArraySink sink(output);
  return snappy::Compress(&source,&sink);
}
extern "C" size_t xe_test_trace_compress_flat(const char* p,size_t n,char* out) {
  snappy::ByteArraySource source(p,n); snappy::UncheckedByteArraySink sink(out);
  return snappy::Compress(&source,&sink);
}

#ifdef XE_TEST_TRACE_LEGACY
#include "xenia/base/filesystem.h"
namespace xe { namespace filesystem {
// Only the baseline's external native-file opener: ASCII fixture paths.
FILE* OpenFile(const std::filesystem::path& path,const std::string_view mode) {
  return std::fopen(path.string().c_str(),std::string(mode).c_str());
}
} }
#endif
