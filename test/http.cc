#include "eventuals/http.h"

#include "event-loop-test.h"
#include "eventuals/eventual.h"
#include "eventuals/interrupt.h"
#include "eventuals/terminal.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

// TODO(benh): build tests using 'HttpMockServer' on Windows once we
// have support for boringssl.
#ifndef _WIN32
#include "test/http-mock-server.h"
#endif

namespace http = eventuals::http;

using eventuals::EventLoop;
using eventuals::Interrupt;
using eventuals::Terminate;

class HttpTest
  : public EventLoopTest,
    public ::testing::WithParamInterface<const char*> {};

const char* schemes[] = {"http://", "https://"};

INSTANTIATE_TEST_SUITE_P(Schemes, HttpTest, testing::ValuesIn(schemes));

// TODO(benh): build tests using 'HttpMockServer' on Windows once we
// have support for boringssl.
#ifndef _WIN32
TEST_P(HttpTest, Get) {
  std::string scheme = GetParam();

  HttpMockServer server(scheme);

  // NOTE: using an 'http::Client' so we can disable peer verification
  // because we have a self-signed certificate.
  http::Client client = http::Client::Builder()
                            .verify_peer(false)
                            .Build();

  EXPECT_CALL(server, ReceivedHeaders)
      .WillOnce([](auto socket, const std::string& data) {
        socket->Send(
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 25\r\n"
            "\r\n"
            "<html>Hello World!</html>\r\n"
            "\r\n");

        socket->Close();
      });

  auto e = client.Get(server.uri());
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().RunUntil(future);

  auto response = future.get();

  EXPECT_EQ(200, response.code);
  EXPECT_EQ("<html>Hello World!</html>", response.body);
}
#endif

TEST_P(HttpTest, GetFailTimeout) {
  std::string scheme = GetParam();

  auto e = http::Get(scheme + "example.com", std::chrono::milliseconds(1));
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_THROW(future.get(), const char*);
}


TEST_P(HttpTest, PostFailTimeout) {
  std::string scheme = GetParam();

  auto e = http::Post(
      scheme + "jsonplaceholder.typicode.com/posts",
      {{"title", "test"},
       {"body", "message"}},
      std::chrono::milliseconds(1));
  auto [future, k] = Terminate(std::move(e));
  k.Start();

  EventLoop::Default().RunUntil(future);

  EXPECT_THROW(future.get(), const char*);
}


TEST_P(HttpTest, GetInterrupt) {
  std::string scheme = GetParam();

  auto e = http::Get(scheme + "example.com");
  auto [future, k] = Terminate(std::move(e));

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  interrupt.Trigger();

  EventLoop::Default().RunUntil(future);

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}


TEST_P(HttpTest, PostInterrupt) {
  std::string scheme = GetParam();

  auto e = http::Post(
      scheme + "jsonplaceholder.typicode.com/posts",
      {{"title", "test"},
       {"body", "message"}});
  auto [future, k] = Terminate(std::move(e));

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  interrupt.Trigger();

  EventLoop::Default().RunUntil(future);

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}


TEST_P(HttpTest, GetInterruptAfterStart) {
  std::string scheme = GetParam();

  auto e = http::Get(scheme + "example.com");
  auto [future, k] = Terminate(std::move(e));

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  // NOTE: now that we've started the continuation 'k' we will have
  // submitted a callback to the event loop and thus by explicitly
  // submitting another callback we will ensure there is a
  // happens-before relationship between starting the transfer
  // and triggering the interrupt.
  EventLoop::Waiter waiter(&EventLoop::Default(), "interrupt.Trigger()");

  EventLoop::Default().Submit(
      [&interrupt]() {
        interrupt.Trigger();
      },
      &waiter);

  EventLoop::Default().RunUntil(future);

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}


TEST_P(HttpTest, PostInterruptAfterStart) {
  std::string scheme = GetParam();

  auto e = http::Post(
      scheme + "jsonplaceholder.typicode.com/posts",
      {{"title", "test"},
       {"body", "message"}});
  auto [future, k] = Terminate(std::move(e));

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  // NOTE: now that we've started the continuation 'k' we will have
  // submitted a callback to the event loop and thus by explicitly
  // submitting another callback we will ensure there is a
  // happens-before relationship between starting the transfer
  // and triggering the interrupt.
  EventLoop::Waiter waiter(&EventLoop::Default(), "interrupt.Trigger()");

  EventLoop::Default().Submit(
      [&interrupt]() {
        interrupt.Trigger();
      },
      &waiter);

  EventLoop::Default().RunUntil(future);

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}
