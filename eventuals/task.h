#pragma once

#include <optional>
#include <tuple>

#include "eventuals/eventual.h"
#include "eventuals/terminal.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

namespace detail {

////////////////////////////////////////////////////////////////////////

template <typename E_>
struct HeapTask {
  using Value_ = typename E_::template ValueFrom<void>;

  template <typename Arg_>
  struct Adaptor {
    Adaptor(
        std::conditional_t<
            std::is_void_v<Value_>,
            Callback<>,
            Callback<Value_>>* start,
        Callback<std::exception_ptr>* fail,
        Callback<>* stop)
      : start_(start),
        fail_(fail),
        stop_(stop) {}

    template <typename... Args>
    void Start(Args&&... args) {
      (*start_)(std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      (*fail_)(
          std::make_exception_ptr(
              std::forward<decltype(args)>(args)...));
    }

    void Stop() {
      (*stop_)();
    }

    void Register(Interrupt&) {}

    std::conditional_t<
        std::is_void_v<Value_>,
        Callback<>,
        Callback<Value_>>* start_;
    Callback<std::exception_ptr>* fail_;
    Callback<>* stop_;
  };

  HeapTask(E_ e)
    : adapted_(
        std::move(e).template k<void>(
            Adaptor<Value_>{&start_, &fail_, &stop_})) {}

  void Start(
      Interrupt& interrupt,
      std::conditional_t<
          std::is_void_v<Value_>,
          Callback<>,
          Callback<Value_>>&& start,
      Callback<std::exception_ptr>&& fail,
      Callback<>&& stop) {
    start_ = std::move(start);
    fail_ = std::move(fail);
    stop_ = std::move(stop);

    // TODO(benh): clarify the semantics of whether or not calling
    // 'Register()' more than once is well-defined.
    adapted_.Register(interrupt);

    adapted_.Start();
  }

  void Fail(
      Interrupt& interrupt,
      std::exception_ptr&& fail_exception,
      std::conditional_t<
          std::is_void_v<Value_>,
          Callback<>,
          Callback<Value_>>&& start,
      Callback<std::exception_ptr>&& fail,
      Callback<>&& stop) {
    start_ = std::move(start);
    fail_ = std::move(fail);
    stop_ = std::move(stop);

    // TODO(benh): clarify the semantics of whether or not calling
    // 'Register()' more than once is well-defined.
    adapted_.Register(interrupt);

    adapted_.Fail(std::move(fail_exception));
  }

  void Stop(
      Interrupt& interrupt,
      std::conditional_t<
          std::is_void_v<Value_>,
          Callback<>,
          Callback<Value_>>&& start,
      Callback<std::exception_ptr>&& fail,
      Callback<>&& stop) {
    start_ = std::move(start);
    fail_ = std::move(fail);
    stop_ = std::move(stop);

    // TODO(benh): clarify the semantics of whether or not calling
    // 'Register()' more than once is well-defined.
    adapted_.Register(interrupt);

    adapted_.Stop();
  }

  std::conditional_t<
      std::is_void_v<Value_>,
      Callback<>,
      Callback<Value_>>
      start_;
  Callback<std::exception_ptr> fail_;
  Callback<> stop_;

  using Adapted_ = decltype(std::declval<E_>().template k<void>(
      std::declval<Adaptor<Value_>>()));

  Adapted_ adapted_;
};

////////////////////////////////////////////////////////////////////////

struct _TaskWith {
  // Since we move lambda function at 'Composable' constructor we need to
  // specify the callback that should be triggered on the produced eventual.
  // For this reason we use 'Action'.
  enum class Action {
    Start = 0,
    Stop = 1,
    Fail = 2,
  };

  template <typename K_, typename Value_, typename... Args_>
  struct Continuation {
    template <typename... Args>
    void Start(Args&&...) {
      std::apply(
          [&](auto&&... args) {
            dispatch_(
                Action::Start,
                std::nullopt,
                std::forward<decltype(args)>(args)...,
                e_,
                *interrupt_,
                [this](auto&&... args) {
                  k_.Start(std::forward<decltype(args)>(args)...);
                },
                [this](std::exception_ptr e) {
                  k_.Fail(std::move(e));
                },
                [this]() {
                  k_.Stop();
                });
          },
          std::move(args_));
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      std::exception_ptr exception;

      if constexpr (sizeof...(args) > 0) {
        exception = std::make_exception_ptr(
            std::forward<decltype(args)>(args)...);
      } else {
        exception = std::make_exception_ptr(
            std::runtime_error("ingress failed (without an error)"));
      }

      std::apply(
          [&](auto&&... args) {
            dispatch_(
                Action::Fail,
                std::move(exception),
                std::forward<decltype(args)>(args)...,
                e_,
                *interrupt_,
                [this](auto&&... args) {
                  k_.Start(std::forward<decltype(args)>(args)...);
                },
                [this](std::exception_ptr e) {
                  k_.Fail(std::move(e));
                },
                [this]() {
                  k_.Stop();
                });
          },
          std::move(args_));
    }

    void Stop() {
      std::apply(
          [&](auto&&... args) {
            dispatch_(
                Action::Stop,
                std::nullopt,
                std::forward<decltype(args)>(args)...,
                e_,
                *interrupt_,
                [this](auto&&... args) {
                  k_.Start(std::forward<decltype(args)>(args)...);
                },
                [this](std::exception_ptr e) {
                  k_.Fail(std::move(e));
                },
                [this]() {
                  k_.Stop();
                });
          },
          std::move(args_));
    }

    void Register(Interrupt& interrupt) {
      interrupt_ = &interrupt;
      k_.Register(interrupt);
    }

    K_ k_;
    std::tuple<Args_...> args_;

    Callback<
        Action,
        std::optional<std::exception_ptr>,
        Args_&&...,
        std::unique_ptr<void, Callback<void*>>&,
        Interrupt&,
        std::conditional_t<
            std::is_void_v<Value_>,
            Callback<>&&,
            Callback<Value_>&&>,
        Callback<std::exception_ptr>&&,
        Callback<>&&>
        dispatch_;

    std::unique_ptr<void, Callback<void*>> e_;
    Interrupt* interrupt_ = nullptr;
  };

  template <typename Value_, typename... Args_>
  struct Composable {
    template <typename>
    using ValueFrom = Value_;

    template <typename F>
    Composable(Args_... args, F f)
      : args_(std::tuple<Args_...>(std::move(args)...)) {
      static_assert(
          std::tuple_size<decltype(args_)>{} > 0 || std::is_invocable_v<F>,
          "'Task' expects a callable that "
          "takes no arguments");

      static_assert(
          std::tuple_size<decltype(args_)>{}
              || std::is_invocable_v<F, Args_...>,
          "'Task' expects a callable that "
          "takes the arguments specified");

      static_assert(
          sizeof(f) <= sizeof(void*),
          "'Task' expects a callable that "
          "can be captured in a 'Callback'");

      using E = decltype(std::apply(f, args_));

      using Value = typename E::template ValueFrom<void>;

      static_assert(
          std::is_convertible_v<Value, Value_>,
          "eventual result type can not be converted into type of 'Task'");

      dispatch_ = [f = std::move(f)](
                      Action action,
                      std::optional<std::exception_ptr> fail_exception,
                      Args_&&... args,
                      std::unique_ptr<void, Callback<void*>>& e_,
                      Interrupt& interrupt,
                      std::conditional_t<
                          std::is_void_v<Value_>,
                          Callback<>&&,
                          Callback<Value_>&&> start,
                      Callback<std::exception_ptr>&& fail,
                      Callback<>&& stop) {
        if (!e_) {
          e_ = std::unique_ptr<void, Callback<void*>>(
              new HeapTask<E>(f(std::move(args)...)),
              [](void* e) {
                delete static_cast<detail::HeapTask<E>*>(e);
              });
        }

        auto* e = static_cast<HeapTask<E>*>(e_.get());

        switch (action) {
          case Action::Start:
            e->Start(
                interrupt,
                std::move(start),
                std::move(fail),
                std::move(stop));
            break;
          case Action::Fail:
            e->Fail(
                interrupt,
                std::move(fail_exception.value()),
                std::move(start),
                std::move(fail),
                std::move(stop));
            break;
          case Action::Stop:
            e->Stop(
                interrupt,
                std::move(start),
                std::move(fail),
                std::move(stop));
            break;
          default:
            LOG(FATAL) << "unreachable";
        }
      };
    }

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K, Value_, Args_...>{
          std::move(k),
          std::move(args_),
          std::move(dispatch_)};
    }

    Callback<
        Action,
        std::optional<std::exception_ptr>,
        Args_&&...,
        std::unique_ptr<void, Callback<void*>>&,
        Interrupt&,
        std::conditional_t<
            std::is_void_v<Value_>,
            Callback<>&&,
            Callback<Value_>&&>,
        Callback<std::exception_ptr>&&,
        Callback<>&&>
        dispatch_;
    std::tuple<Args_...> args_;
  };
};

////////////////////////////////////////////////////////////////////////

} // namespace detail

////////////////////////////////////////////////////////////////////////

template <typename Value_>
class Task {
 public:
  template <typename Arg>
  using ValueFrom = Value_;

  template <typename... Args>
  using With = detail::_TaskWith::Composable<Value_, Args...>;

  template <typename Arg, typename K>
  auto k(K k) && {
    return std::move(task_).template k<Arg>(std::move(k));
  }

  template <typename F>
  Task(F f)
    : task_(std::move(f)) {}

  auto operator*() && {
    auto [future, k] = Terminate(std::move(*this));

    k.Start();

    return future.get();
  }

 private:
  detail::_TaskWith::Composable<Value_> task_;
};

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////