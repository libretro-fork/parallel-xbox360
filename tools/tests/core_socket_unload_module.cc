#include "xenia/kernel/socket_io_context.h"
namespace {
struct Completion {
  void (*retired)();
  ~Completion() { retired(); }
};
struct Probe {
  std::shared_ptr<xe::kernel::SocketIoContext> context;
  xe_socket_link link{};
  std::optional<asio::ip::udp::socket> socket;
  std::shared_ptr<Completion> completion;
  Probe(std::shared_ptr<xe::kernel::SocketIoContext> owner, void (*retired)())
      : context(std::move(owner)), completion(std::make_shared<Completion>()) {
    completion->retired = retired;
    socket.emplace(context->get());
    socket->open(asio::ip::udp::v4());
    socket->bind(asio::ip::udp::endpoint(asio::ip::address_v4::loopback(), 0));
    context->Register(&link, this,
        [](void* opaque) { static_cast<Probe*>(opaque)->socket.reset(); },
        [](void* opaque) { static_cast<Probe*>(opaque)->completion.reset(); });
    socket->async_wait(asio::socket_base::wait_read,
        [keep = completion](const asio::error_code&) {});
  }
  ~Probe() { context->Unregister(&link); socket.reset(); }
};
std::shared_ptr<xe::kernel::SocketIoContext> owner;
std::unique_ptr<Probe> retained;
}
extern "C" __declspec(dllexport) void ProbeStart(void (*retired)()) {
  owner = std::make_shared<xe::kernel::SocketIoContext>();
  retained = std::make_unique<Probe>(owner, retired);
}
extern "C" __declspec(dllexport) void ProbeStop() {
  owner->Shutdown();
  owner.reset();
  // The socket shell deliberately lives until DLL detach.
}