#pragma once

#include <type_traits>
#include <utility>

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename T_, typename E_>
struct _TypeCheck {
  template <typename Arg>
  using ValueFrom = typename E_::template ValueFrom<Arg>;

  template <typename Arg, typename Errors>
  using ErrorsFrom = typename E_::template ErrorsFrom<Arg, Errors>;

  template <typename Arg, typename K>
  auto k(K k) && {
    static_assert(
        std::is_same_v<T_, ValueFrom<Arg>>,
        "Failed to type check; expecting type on left, found type on right");

    return std::move(e_).template k<Arg>(std::move(k));
  }

  // Flags that forbid non-composable things, i.e., a "stream"
  // with an eventual that can not stream or a "loop" with
  // something that is not streaming.
  static constexpr bool Streaming = true;
  static constexpr bool Looping = false;
  static constexpr bool IsEventual = true;

  E_ e_;
};

////////////////////////////////////////////////////////////////////////

template <typename T, typename E>
[[nodiscard]] auto TypeCheck(E e) {
  return _TypeCheck<T, E>{std::move(e)};
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
