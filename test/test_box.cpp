#include <moko3./moko3.hpp>
#include "metel/box.hpp"

struct noncomparable {
  int x;
};

TEST("box") {
  static_assert(std::equality_comparable<metel::box<int>>);
  static_assert(!std::equality_comparable<metel::box<noncomparable>>);
  using metel::box;
  box<std::vector<int>> b;
  b.emplace({1, 2, 3});
  std::vector<int> exp{1, 2, 3};
  REQUIRE(exp == b);
  REQUIRE(exp == *b);
  using T = std::tuple<std::initializer_list<int>, int&&, int&, std::string>;
  box b2 = b;
  REQUIRE(b2 == b);
  REQUIRE(*b2 == *b);
  static_assert(std::three_way_comparable<decltype(b), std::strong_ordering>);
  static_assert(!std::three_way_comparable<box<noncomparable>, std::strong_ordering>);
  static_assert(std::is_copy_constructible_v<decltype(b)>);
  static_assert(!std::is_copy_constructible_v<box<std::unique_ptr<int>>>);
  static_assert(!std::is_copy_assignable_v<box<std::unique_ptr<int>>>);
  struct nonmove {
    nonmove() {
    }
    nonmove(nonmove&&) = delete;
    void operator=(nonmove&&) = delete;
  };
  static_assert(std::is_move_assignable_v<box<nonmove>>);
  static_assert(std::is_move_constructible_v<box<nonmove>>);
  REQUIRE(b <=> b2 == std::strong_ordering::equal);
  box<T> hmm;
  int i = 0;
  // корректно прокидываются ссылки
  hmm.emplace({0}, 5, i, std::string{});
}

MOKO3_MAIN;
