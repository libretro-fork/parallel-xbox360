#ifndef XENIA_KERNEL_SOCKET_IO_CONTEXT_H_
#define XENIA_KERNEL_SOCKET_IO_CONTEXT_H_
/* Include Asio before Windows headers. */
#include <asio.hpp>
#include <memory>
#include <optional>
#include "libretro/core_socket_worker.h"
namespace xe { namespace kernel {
class SocketIoContext {
 public:
  SocketIoContext();
  ~SocketIoContext();
  asio::io_context& get() { return *context_; }
  bool Register(xe_socket_link* link, void* object, void (*cancel)(void*), void (*retire)(void*));
  void Unregister(xe_socket_link* link);
  void Shutdown();
 private:
  static void Run(void* opaque);
  static void Stop(void* opaque);
  std::unique_ptr<asio::io_context> context_;
  std::optional<asio::executor_work_guard<asio::io_context::executor_type>> work_;
  xe_socket_worker* worker_ = nullptr;
};
} }
#endif