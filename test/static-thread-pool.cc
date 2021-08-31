#include "stout/static-thread-pool.h"

#include "gtest/gtest.h"
#include "stout/closure.h"
#include "stout/eventual.h"
#include "stout/loop.h"
#include "stout/map.h"
#include "stout/repeat.h"
#include "stout/terminal.h"
#include "stout/then.h"
#include "stout/until.h"

namespace eventuals = stout::eventuals;

using stout::eventuals::Closure;
using stout::eventuals::Eventual;
using stout::eventuals::Loop;
using stout::eventuals::Map;
using stout::eventuals::Pinned;
using stout::eventuals::Repeat;
using stout::eventuals::StaticThreadPool;
using stout::eventuals::Then;
using stout::eventuals::Until;

TEST(StaticThreadPoolTest, Schedulable) {
  struct Foo : public StaticThreadPool::Schedulable {
    Foo()
      : StaticThreadPool::Schedulable(Pinned(
          std::thread::hardware_concurrency() - 1)) {}

    auto Operation() {
      return Schedule(
                 Then([this]() {
                   return i;
                 }))
          | Then([](auto i) {
               return i + 1;
             });
    }

    int i = 41;
  };

  Foo foo;

  EXPECT_EQ(42, *foo.Operation());
}


TEST(StaticThreadPoolTest, Reschedulable) {
  StaticThreadPool::Requirements requirements("reschedulable");
  auto e = [&]() {
    return StaticThreadPool::Scheduler().Schedule(
        &requirements,
        Closure([id = std::this_thread::get_id()]() mutable {
          EXPECT_NE(id, std::this_thread::get_id());
          id = std::this_thread::get_id();
          return Eventual<void>()
                     .start([&id](auto& k) {
                       EXPECT_EQ(id, std::this_thread::get_id());
                       auto thread = std::thread(
                           [&id, &k]() {
                             EXPECT_NE(id, std::this_thread::get_id());
                             k.Start();
                           });
                       thread.detach();
                     })
              | Eventual<void>()
                    .start([&id](auto& k) {
                      EXPECT_EQ(id, std::this_thread::get_id());
                      k.Start();
                    });
        }));
  };

  *e();
}


TEST(StaticThreadPoolTest, PingPong) {
  struct Streamer : public StaticThreadPool::Schedulable {
    Streamer(Pinned pinned)
      : StaticThreadPool::Schedulable(pinned) {}

    auto Stream() {
      using namespace eventuals;

      return Repeat()
          | Until([this]() {
               return Schedule(Then([this]() {
                 return count > 5;
               }));
             })
          | Map(Schedule(Then([this]() mutable {
               return count++;
             })));
    }

    int count = 0;
  };

  struct Listener : public StaticThreadPool::Schedulable {
    Listener(Pinned pinned)
      : StaticThreadPool::Schedulable(pinned) {}

    auto Listen() {
      using namespace eventuals;

      return Map(Schedule(Then([this](int i) {
               count++;
               return i;
             })))
          | Loop()
          | Then([this](auto&&...) {
               return count;
             });
    }

    size_t count = 0;
  };

  Streamer streamer(Pinned(0));
  Listener listener(Pinned(1));

  EXPECT_EQ(6, *(streamer.Stream() | listener.Listen()));
}
