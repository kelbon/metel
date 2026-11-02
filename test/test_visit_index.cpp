#include <moko3/moko3.hpp>

#include "metel/meta.hpp"

#include <array>

// индексы с компиляции -> рантайм: f<I>() возвращает I
template <size_t Max>
static bool visits_all() {
  for (size_t i = 0; i <= Max; ++i)
    if (metel::visit_index<Max>([]<size_t I>() { return I; }, i) != i)
      return false;
  return true;
}

TEST("visit_index") {
  REQUIRE(visits_all<0>());
  REQUIRE(visits_all<5>());
  REQUIRE(visits_all<metel::VISIT_INDEX_MAX>());
  // больше одного switch: границы кусков и хвост
  REQUIRE(visits_all<metel::VISIT_INDEX_MAX + 1>());
  REQUIRE(visits_all<300>());
  static_assert(metel::visit_index<300>([]<size_t I>() { return I * 2; }, 257) == 514);
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
