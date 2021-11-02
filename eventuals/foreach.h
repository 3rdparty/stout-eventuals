#pragma once

#include "eventuals/loop.h"
#include "eventuals/map.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {

////////////////////////////////////////////////////////////////////////

template <typename E, typename F>
auto Foreach(E e, F f) {
  return std::move(e)
      | Map(std::move(f))
      | Loop();
}

////////////////////////////////////////////////////////////////////////

} // namespace eventuals

////////////////////////////////////////////////////////////////////////
