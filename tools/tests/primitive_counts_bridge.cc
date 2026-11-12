/* Existing primitive-range implementations through a typed test ABI only. */
#include "xenia/gpu/primitive_processor.h"
DECLARE_bool(force_convert_triangle_fans_to_lists);
DECLARE_bool(force_convert_line_loops_to_strips);
DECLARE_bool(force_convert_quad_lists_to_triangle_lists);
DECLARE_bool(force_convert_triangle_strips_to_lists);
namespace xe {
namespace gpu {
class PrimitiveProcessorTestAccess : public PrimitiveProcessor {
 public:
  static uint32_t Run(unsigned topology,unsigned width,const void* source,
      unsigned count,uint32_t reset,uint32_t mask,uint32_t* output,
      unsigned capacity,unsigned* used) {
    const xe_primitive_index_count getters[4]={GetTriangleFanListIndexCount,
        GetLineLoopStripIndexCount,GetQuadListTriangleListIndexCount,
        GetTriangleStripListIndexCount};
    if(topology>=4 || (width!=2 && width!=4))return UINT32_MAX;
    std::deque<SinglePrimitiveRange> ranges;
    uint32_t total=width==2?GetMultiPrimitiveHostIndexCountAndRanges(
        getters[topology],static_cast<const uint16_t*>(source),count,
        uint16_t(reset),ranges):GetMultiPrimitiveHostIndexCountAndRanges(
        getters[topology],static_cast<const uint32_t*>(source),count,reset,mask,ranges);
    if(ranges.size()>capacity)return UINT32_MAX;
    *used=unsigned(ranges.size());
    for(const auto& range:ranges) {
      *output++=range.guest_offset;*output++=range.guest_index_count;
      *output++=range.host_index_count;
    }
    return total;
  }
};
}  // namespace gpu
}  // namespace xe
extern "C" uint32_t xe_primitive_count_ranges(unsigned topology,unsigned width,
    const void* source,unsigned count,uint32_t reset,uint32_t mask,
    uint32_t* output,unsigned capacity,unsigned* used) {
  return xe::gpu::PrimitiveProcessorTestAccess::Run(topology,width,source,count,reset,mask,output,capacity,used);
}
class PrimitiveSharedMemory final : public xe::gpu::SharedMemory {
 public:
  explicit PrimitiveSharedMemory(xe::Memory& memory):SharedMemory(memory) {}
 private:
  bool UploadRanges(xe_upload_range*,uint32_t) override { return false; }
};
class PrimitiveInitAccess final : public xe::gpu::PrimitiveProcessor {
 public:
  void* mapping;
  size_t capacity,size=0;
  unsigned calls=0;
  int fail;
  xe_primitive_builtin_layout result={{SIZE_MAX,SIZE_MAX,SIZE_MAX,SIZE_MAX},0};
  PrimitiveInitAccess(const xe::gpu::RegisterFile& regs,xe::Memory& memory,
      xe::gpu::SharedMemory& shared,void* data,size_t bytes,int failing)
      :PrimitiveProcessor(regs,memory,shared),mapping(data),capacity(bytes),fail(failing) {}
  bool Run(unsigned flags) {
    return InitializeCommon(true,(flags&1)!=0,(flags&2)!=0,(flags&4)!=0,
        (flags&8)!=0,(flags&16)!=0);
  }
 private:
  bool InitializeBuiltinIndexBuffer(size_t bytes,const xe_primitive_builtin_layout& layout) override {
    ++calls;size=bytes;result=layout;
    return !fail && bytes<=capacity && xe_primitive_builtin_fill(mapping,bytes,&layout)!=0;
  }
  void* RequestHostConvertedIndexBufferForCurrentFrame(xe::gpu::xenos::IndexFormat,
      uint32_t,bool,uint32_t,size_t&) override { return nullptr; }
};
extern "C" int xe_primitive_initialize(unsigned flags,void* mapping,size_t capacity,
    xe_primitive_builtin_layout* layout,size_t* size,unsigned* calls,int fail) {
  const bool old_fans=cvars::force_convert_triangle_fans_to_lists;
  const bool old_lines=cvars::force_convert_line_loops_to_strips;
  const bool old_quads=cvars::force_convert_quad_lists_to_triangle_lists;
  const bool old_strips=cvars::force_convert_triangle_strips_to_lists;
  cvars::force_convert_triangle_fans_to_lists=false;
  cvars::force_convert_line_loops_to_strips=false;
  cvars::force_convert_quad_lists_to_triangle_lists=false;
  cvars::force_convert_triangle_strips_to_lists=(flags&32)!=0;
  xe::Memory memory;
  xe::gpu::RegisterFile registers;
  PrimitiveSharedMemory shared(memory);
  PrimitiveInitAccess processor(registers,memory,shared,mapping,capacity,fail);
  const bool ok=processor.Run(flags);
  *layout=processor.result;*size=processor.size;*calls=processor.calls;
  cvars::force_convert_triangle_fans_to_lists=old_fans;
  cvars::force_convert_line_loops_to_strips=old_lines;
  cvars::force_convert_quad_lists_to_triangle_lists=old_quads;
  cvars::force_convert_triangle_strips_to_lists=old_strips;
  return ok?1:0;
}
