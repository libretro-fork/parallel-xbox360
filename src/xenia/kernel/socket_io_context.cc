#include "xenia/kernel/socket_io_context.h"
#include <new>
namespace xe { namespace kernel {
SocketIoContext::SocketIoContext()
    : context_(std::make_unique<asio::io_context>()),
      work_(asio::make_work_guard(*context_)) {
  worker_ = xe_socket_worker_new(Run, Stop, this);
  if (!worker_) throw std::bad_alloc();
}
SocketIoContext::~SocketIoContext() {
  Shutdown();
  xe_socket_worker_free(worker_);
}
bool SocketIoContext::Register(xe_socket_link* link, void* object,
                               void (*cancel)(void*), void (*retire)(void*)) {
  return xe_socket_worker_register(worker_, link, object, cancel, retire) != 0;
}
void SocketIoContext::Unregister(xe_socket_link* link) {
  xe_socket_worker_unregister(worker_, link);
}
void SocketIoContext::Run(void* opaque) {
  static_cast<SocketIoContext*>(opaque)->context_->run();
}
void SocketIoContext::Stop(void* opaque) {
  auto* self = static_cast<SocketIoContext*>(opaque);
  self->work_.reset();
  self->context_->stop();
}
void SocketIoContext::Shutdown() {
  xe_socket_worker_stop(worker_);
  // Release pending handlers while their guest objects still have a kernel.
  context_.reset();
}
} }