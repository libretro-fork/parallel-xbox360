/* Native Snappy ABI only; storage, bounds and transfers are C89-owned. */
#ifndef XENIA_GPU_TRACE_IO_ADAPTER_H_
#define XENIA_GPU_TRACE_IO_ADAPTER_H_
#include "libretro/core_gpu_trace.h"
#include "third_party/snappy/snappy-sinksource.h"
namespace xe { namespace gpu {
class TraceSource final : public snappy::Source {
 public:
  TraceSource(const void* a,size_t an,const void* b,size_t bn) {
    xe_trace_source_init(&source_,a,an,b,bn);
  }
  size_t Available() const override { return source_.remaining; }
  const char* Peek(size_t* n) override { return xe_trace_source_peek(&source_,n); }
  void Skip(size_t n) override { xe_trace_source_skip(&source_,n); }
 private:
  xe_trace_source source_;
};
class TraceSink final : public snappy::Sink {
 public:
  explicit TraceSink(xe_trace_stream* stream) : stream_(stream) {}
  void Append(const char* bytes,size_t n) override { xe_trace_stream_write(stream_,bytes,n); }
 private:
  xe_trace_stream* stream_;
};
} }
#endif
