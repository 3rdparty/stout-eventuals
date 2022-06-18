#pragma once

#include <atomic>

#include "asio.hpp"
#include "eventuals/event-loop.h"
#include "stout/borrowed_ptr.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {
namespace ip {
namespace tcp {

////////////////////////////////////////////////////////////////////////

enum class Protocol {
  IPV4,
  IPV6
};

////////////////////////////////////////////////////////////////////////

// Different ways a socket may be shutdown.
enum class ShutdownType {
  // Shutdown the send side of the socket.
  SEND = asio::ip::tcp::socket::shutdown_type::shutdown_send,

  // Shutdown the receive side of the socket.
  RECEIVE = asio::ip::tcp::socket::shutdown_type::shutdown_receive,

  // Shutdown both send and receive on the socket.
  BOTH = asio::ip::tcp::socket::shutdown_type::shutdown_both,
};

////////////////////////////////////////////////////////////////////////

class SocketBase {
 public:
  SocketBase() = delete;

  virtual ~SocketBase() {
    CHECK(!IsOpen()) << "Close the socket before destructing";
  }

  [[nodiscard]] auto Open();

  [[nodiscard]] auto Bind(std::string&& ip, uint16_t port);

  [[nodiscard]] auto Connect(std::string&& ip, uint16_t port);

  [[nodiscard]] auto Shutdown(ShutdownType shutdown_type);

  // NOTE: It's not possible to implement a virtual auto
  // method, so we have to omit a Close() method here
  // and make its implementations in Socket and ssl::Socket.
  // TODO: Implement Close() method here instead of being
  // two different implementations for Socket and ssl::Socket.

  bool IsOpen() {
    return is_open_.load();
  }

 protected:
  SocketBase(Protocol protocol, EventLoop& loop = EventLoop::Default())
    : loop_(loop),
      protocol_(protocol) {}

  virtual asio::ip::tcp::socket& socket_handle() = 0;

  asio::io_context& io_context() {
    return loop_.io_context();
  }

  EventLoop& loop_;
  // asio::ip::tcp::socket::is_open() method is not thread-safe,
  // so we store the state in an atomic variable by ourselves.
  std::atomic<bool> is_open_ = false;

  // This variable is only accessed or modified inside event loop,
  // so we don't need std::atomic wrapper.
  bool is_connected_ = false;

  Protocol protocol_;

  friend class Acceptor;
};

////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto SocketBase::Open() {
  return loop_.Schedule(
      Eventual<void>()
          .interruptible()
          .raises<std::runtime_error>()
          .context(this)
          .start([](auto& socket, auto& k, Interrupt::Handler& handler) {
            asio::post(
                socket->io_context(),
                [&]() {
                  if (handler.interrupt().Triggered()) {
                    k.Stop();
                    return;
                  }

                  if (socket->IsOpen()) {
                    k.Fail(std::runtime_error("Socket is already opened"));
                    return;
                  }

                  asio::error_code error;

                  switch (socket->protocol_) {
                    case Protocol::IPV4:
                      socket->socket_handle().open(
                          asio::ip::tcp::v4(),
                          error);
                      break;
                    case Protocol::IPV6:
                      socket->socket_handle().open(
                          asio::ip::tcp::v6(),
                          error);
                      break;
                  }

                  if (!error) {
                    socket->is_open_.store(true);
                    k.Start();
                  } else {
                    k.Fail(std::runtime_error(error.message()));
                  }
                });
          }));
}

////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto SocketBase::Bind(std::string&& ip, uint16_t port) {
  struct Data {
    SocketBase* socket;
    std::string ip;
    uint16_t port;
  };

  return loop_.Schedule(
      Eventual<void>()
          .interruptible()
          .raises<std::runtime_error>()
          .context(Data{this, std::move(ip), port})
          .start([](auto& data, auto& k, Interrupt::Handler& handler) {
            asio::post(
                data.socket->io_context(),
                [&]() {
                  if (handler.interrupt().Triggered()) {
                    k.Stop();
                    return;
                  }

                  if (!data.socket->socket_handle().is_open()) {
                    k.Fail(std::runtime_error("Socket is closed"));
                    return;
                  }

                  if (data.socket->is_connected_) {
                    k.Fail(
                        std::runtime_error(
                            "Bind call is forbidden "
                            "while socket is connected"));
                    return;
                  }

                  asio::error_code error;
                  asio::ip::tcp::endpoint endpoint;

                  switch (data.socket->protocol_) {
                    case Protocol::IPV4:
                      endpoint = asio::ip::tcp::endpoint(
                          asio::ip::make_address_v4(data.ip, error),
                          data.port);
                      break;
                    case Protocol::IPV6:
                      endpoint = asio::ip::tcp::endpoint(
                          asio::ip::make_address_v6(data.ip, error),
                          data.port);
                      break;
                  }


                  if (error) {
                    k.Fail(std::runtime_error(error.message()));
                    return;
                  }

                  data.socket->socket_handle().bind(endpoint, error);

                  if (!error) {
                    k.Start();
                  } else {
                    k.Fail(std::runtime_error(error.message()));
                  }
                });
          }));
}

////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto SocketBase::Connect(
    std::string&& ip,
    uint16_t port) {
  struct Data {
    SocketBase* socket;
    std::string ip;
    uint16_t port;

    // Used for interrupt handler due to
    // static_assert(sizeof(Handler<F>) <= SIZE) (callback.h(59,5))
    // requirement for handler.Install().
    void* k = nullptr;

    bool started = false;
    bool completed = false;
  };

  return loop_.Schedule(
      Eventual<void>()
          .interruptible()
          .raises<std::runtime_error>()
          .context(Data{this, std::move(ip), port})
          .start([](auto& data, auto& k, Interrupt::Handler& handler) {
            using K = std::decay_t<decltype(k)>;
            data.k = &k;

            handler.Install([&data]() {
              asio::post(data.socket->io_context(), [&]() {
                K& k = *static_cast<K*>(data.k);

                if (!data.started) {
                  data.completed = true;
                  k.Stop();
                } else if (!data.completed) {
                  data.completed = true;
                  asio::error_code error;
                  data.socket->socket_handle().cancel(error);

                  if (!error) {
                    k.Stop();
                  } else {
                    k.Fail(std::runtime_error(error.message()));
                  }
                }
              });
            });

            asio::post(
                data.socket->io_context(),
                [&]() {
                  if (!data.completed) {
                    if (handler.interrupt().Triggered()) {
                      data.completed = true;
                      k.Stop();
                      return;
                    }

                    CHECK(!data.started);
                    data.started = true;

                    if (!data.socket->socket_handle().is_open()) {
                      data.completed = true;
                      k.Fail(std::runtime_error("Socket is closed"));
                      return;
                    }

                    if (data.socket->is_connected_) {
                      data.completed = true;
                      k.Fail(
                          std::runtime_error(
                              "Socket is already connected"));
                      return;
                    }

                    asio::error_code error;
                    asio::ip::tcp::endpoint endpoint;

                    switch (data.socket->protocol_) {
                      case Protocol::IPV4:
                        endpoint = asio::ip::tcp::endpoint(
                            asio::ip::make_address_v4(data.ip, error),
                            data.port);
                        break;
                      case Protocol::IPV6:
                        endpoint = asio::ip::tcp::endpoint(
                            asio::ip::make_address_v6(data.ip, error),
                            data.port);
                        break;
                    }

                    if (error) {
                      data.completed = true;
                      k.Fail(std::runtime_error(error.message()));
                      return;
                    }

                    data.socket->socket_handle().async_connect(
                        endpoint,
                        [&](const asio::error_code& error) {
                          if (!data.completed) {
                            data.completed = true;

                            if (!error) {
                              data.socket->is_connected_ = true;
                              k.Start();
                            } else {
                              k.Fail(std::runtime_error(error.message()));
                            }
                          }
                        });
                  }
                });
          }));
}

////////////////////////////////////////////////////////////////////////

[[nodiscard]] inline auto SocketBase::Shutdown(ShutdownType shutdown_type) {
  struct Data {
    SocketBase* socket;
    ShutdownType shutdown_type;
  };

  return loop_.Schedule(
      Eventual<void>()
          .interruptible()
          .raises<std::runtime_error>()
          .context(Data{this, shutdown_type})
          .start([](auto& data, auto& k, Interrupt::Handler& handler) {
            asio::post(
                data.socket->io_context(),
                [&]() {
                  if (handler.interrupt().Triggered()) {
                    k.Stop();
                    return;
                  }

                  if (!data.socket->IsOpen()) {
                    k.Fail(std::runtime_error("Socket is closed"));
                    return;
                  }

                  asio::error_code error;

                  data.socket->socket_handle().shutdown(
                      static_cast<
                          asio::socket_base::shutdown_type>(
                          data.shutdown_type),
                      error);

                  if (!error) {
                    k.Start();
                  } else {
                    k.Fail(std::runtime_error(error.message()));
                  }
                });
          }));
}

////////////////////////////////////////////////////////////////////////

} // namespace tcp
} // namespace ip
} // namespace eventuals

////////////////////////////////////////////////////////////////////////
