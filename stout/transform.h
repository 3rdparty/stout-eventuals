#pragma once

#include "stout/interrupt.h"
#include "stout/loop.h"
#include "stout/terminal.h"

////////////////////////////////////////////////////////////////////////

namespace stout {
namespace eventuals {

////////////////////////////////////////////////////////////////////////

// Forward declaration to break circular dependency with stream.h.
template <typename K>
void next(K& k);

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template <
  typename K_,
  typename Context_,
  typename Start_,
  typename Body_,
  typename Ended_,
  typename Fail_,
  typename Stop_,
  typename Interrupt_,
  typename Value_,
  typename... Errors_>
struct Transform
{
  using Value = typename ValueFrom<K_, Value_>::type;

  K_ k_;

  Context_ context_;
  Start_ start_;
  Body_ body_;
  Ended_ ended_;
  Fail_ fail_;
  Stop_ stop_;
  Interrupt_ interrupt_;

  std::optional<Interrupt::Handler> handler_;

  template <
    typename Value,
    typename... Errors,
    typename K,
    typename Context,
    typename Start,
    typename Body,
    typename Ended,
    typename Fail,
    typename Stop,
    typename Interrupt>
  static auto create(
      K k,
      Context context,
      Start start,
      Body body,
      Ended ended,
      Fail fail,
      Stop stop,
      Interrupt interrupt)
  {
    return Transform<
      K,
      Context,
      Start,
      Body,
      Ended,
      Fail,
      Stop,
      Interrupt,
      Value,
      Errors...> {
      std::move(k),
      std::move(context),
      std::move(start),
      std::move(body),
      std::move(ended),
      std::move(fail),
      std::move(stop),
      std::move(interrupt)
    };
  }

  template <typename K>
  auto k(K k) &&
  {
    return create<Value_, Errors_...>(
        [&]() {
          if constexpr (!IsUndefined<K_>::value) {
            return std::move(k_) | std::move(k);
          } else {
            return std::move(k);
          }
        }(),
        std::move(context_),
        std::move(start_),
        std::move(body_),
        std::move(ended_),
        std::move(fail_),
        std::move(stop_),
        std::move(interrupt_));
  }

  template <typename Context>
  auto context(Context context) &&
  {
    static_assert(IsUndefined<Context_>::value, "Duplicate 'context'");
    return create<Value_, Errors_...>(
        std::move(k_),
        std::move(context),
        std::move(start_),
        std::move(body_),
        std::move(ended_),
        std::move(fail_),
        std::move(stop_),
        std::move(interrupt_));
  }

  template <typename Start>
  auto start(Start start) &&
  {
    static_assert(IsUndefined<Start_>::value, "Duplicate 'start'");
    return create<Value_, Errors_...>(
        std::move(k_),
        std::move(context_),
        std::move(start),
        std::move(body_),
        std::move(ended_),
        std::move(fail_),
        std::move(stop_),
        std::move(interrupt_));
  }

  template <typename Body>
  auto body(Body body) &&
  {
    static_assert(IsUndefined<Body_>::value, "Duplicate 'body'");
    return create<Value_, Errors_...>(
        std::move(k_),
        std::move(context_),
        std::move(start_),
        std::move(body),
        std::move(ended_),
        std::move(fail_),
        std::move(stop_),
        std::move(interrupt_));
  }

  template <typename Ended>
  auto ended(Ended ended) &&
  {
    static_assert(IsUndefined<Ended_>::value, "Duplicate 'ended'");
    return create<Value_, Errors_...>(
        std::move(k_),
        std::move(context_),
        std::move(start_),
        std::move(body_),
        std::move(ended),
        std::move(fail_),
        std::move(stop_),
        std::move(interrupt_));
  }

  template <typename Fail>
  auto fail(Fail fail) &&
  {
    static_assert(IsUndefined<Fail_>::value, "Duplicate 'fail'");
    return create<Value_, Errors_...>(
        std::move(k_),
        std::move(context_),
        std::move(start_),
        std::move(body_),
        std::move(ended_),
        std::move(fail),
        std::move(stop_),
        std::move(interrupt_));
  }

  template <typename Stop>
  auto stop(Stop stop) &&
  {
    static_assert(IsUndefined<Stop_>::value, "Duplicate 'stop'");
    return create<Value_, Errors_...>(
        std::move(k_),
        std::move(context_),
        std::move(start_),
        std::move(body_),
        std::move(ended_),
        std::move(fail_),
        std::move(stop),
        std::move(interrupt_));
  }

  template <typename Interrupt>
  auto interrupt(Interrupt interrupt) &&
  {
    static_assert(IsUndefined<Interrupt_>::value, "Duplicate 'interrupt'");
    return create<Value_, Errors_...>(
        std::move(k_),
        std::move(context_),
        std::move(start_),
        std::move(body_),
        std::move(ended_),
        std::move(fail_),
        std::move(stop_),
        std::move(interrupt));
  }

  template <typename... Args>
  void Start(Args&&... args)
  {
    auto interrupted = [this]() mutable {
      if (handler_) {
        return !handler_->Install();
      } else {
        return false;
      }
    }();

    if (interrupted) {
      assert(handler_);
      handler_->Invoke();
    } else {
      if constexpr (IsUndefined<Start_>::value) {
        eventuals::succeed(k_, std::forward<Args>(args)...);
      } else if constexpr (IsUndefined<Context_>::value) {
        start_(k_, std::forward<Args>(args)...);
      } else {
        start_(context_, k_, std::forward<Args>(args)...);
      }
    }
  }

  template <typename... Args>
  void Fail(Args&&... args)
  {
    if constexpr (IsUndefined<Start_>::value) {
      eventuals::fail(k_, std::forward<Args>(args)...);
    } else if constexpr (IsUndefined<Context_>::value) {
      fail_(k_, std::forward<Args>(args)...);
    } else {
      fail_(context_, k_, std::forward<Args>(args)...);
    }
  }

  void Stop()
  {
    if constexpr (IsUndefined<Start_>::value) {
      eventuals::stop(k_);
    } else if constexpr (IsUndefined<Context_>::value) {
      stop_(k_);
    } else {
      stop_(context_, k_);
    }
  }

  void Register(Interrupt& interrupt)
  {
    k_.Register(interrupt);

    if constexpr (!IsUndefined<Interrupt_>::value) {
      handler_.emplace(&interrupt, [this]() {
        if constexpr (IsUndefined<Context_>::value) {
          interrupt_(k_);
        } else {
          interrupt_(context_, k_);
        }
      });
    }
  }

  template <typename... Args>
  void Body(Args&&... args)
  {
    if constexpr (IsUndefined<Body_>::value) {
      eventuals::body(k_, std::forward<Args>(args)...);
    } else if constexpr (IsUndefined<Context_>::value) {
      body_(k_, std::forward<Args>(args)...);
    } else {
      body_(context_, k_, std::forward<Args>(args)...);
    }
  }

  void Ended()
  {
    if constexpr (IsUndefined<Ended_>::value) {
      eventuals::ended(k_);
    } else if constexpr (IsUndefined<Context_>::value) {
      ended_(k_);
    } else {
      ended_(context_, k_);
    }
  }
};

////////////////////////////////////////////////////////////////////////

} // namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename>
struct IsTransform : std::false_type {};

////////////////////////////////////////////////////////////////////////

template <
  typename K,
  typename Context,
  typename Start,
  typename Body,
  typename Ended,
  typename Fail,
  typename Stop,
  typename Interrupt,
  typename Value,
  typename... Errors>
struct IsTransform<
  detail::Transform<
    K,
    Context,
    Start,
    Body,
    Ended,
    Fail,
    Stop,
    Interrupt,
    Value,
    Errors...>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <
  typename K,
  typename Context,
  typename Start,
  typename Body,
  typename Ended,
  typename Fail,
  typename Stop,
  typename Interrupt,
  typename Value,
  typename... Errors>
struct IsContinuation<
  detail::Transform<
    K,
    Context,
    Start,
    Body,
    Ended,
    Fail,
    Stop,
    Interrupt,
    Value,
    Errors...>> : std::true_type {};

////////////////////////////////////////////////////////////////////////

template <
  typename K,
  typename Context,
  typename Start,
  typename Body,
  typename Ended,
  typename Fail,
  typename Stop,
  typename Interrupt,
  typename Value,
  typename... Errors>
struct HasLoop<
  detail::Transform<
    K,
    Context,
    Start,
    Body,
    Ended,
    Fail,
    Stop,
    Interrupt,
    Value,
    Errors...>> : HasLoop<K> {};

////////////////////////////////////////////////////////////////////////

template <
  typename K,
  typename Context,
  typename Start,
  typename Body,
  typename Ended,
  typename Fail,
  typename Stop,
  typename Interrupt,
  typename Value,
  typename... Errors>
struct HasTerminal<
  detail::Transform<
    K,
    Context,
    Start,
    Body,
    Ended,
    Fail,
    Stop,
    Interrupt,
    Value,
    Errors...>> : HasTerminal<K> {};

////////////////////////////////////////////////////////////////////////

template <typename Value, typename... Errors>
auto Transform()
{
  return detail::Transform<
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Undefined,
    Value,
    Errors...> {
    Undefined(),
    Undefined(),
    Undefined(),
    Undefined(),
    Undefined(),
    Undefined(),
    Undefined(),
    Undefined()
  };
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals {
} // namespace stout {

////////////////////////////////////////////////////////////////////////
