#include "stout/collect.h"

#include <set>
#include <vector>

#include "gtest/gtest.h"
#include "stout/iterate.h"
#include "stout/terminal.h"

namespace eventuals = stout::eventuals;

using stout::eventuals::Collect;
using stout::eventuals::Iterate;

TEST(Collect, CommonVectorPass) {
  std::vector<int> v = {5, 12};

  auto s = [&]() {
    return Iterate(v)
        | Collect<std::vector<int>>();
  };

  std::vector<int> result = *s();

  EXPECT_EQ(5, result.at(0));
  EXPECT_EQ(12, result.at(1));
  EXPECT_EQ(2, result.size());
}

TEST(Collect, CommonSetPass) {
  std::set<int> v = {5, 12};

  auto s = [&]() {
    return Iterate(v)
        | Collect<std::set<int>>();
  };

  std::set<int> result = *s();

  ASSERT_EQ(2, result.size());
  EXPECT_EQ(5, *result.begin());
  EXPECT_EQ(12, *++result.begin());
}
