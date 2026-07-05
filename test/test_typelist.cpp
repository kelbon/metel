#include <moko3/moko3.hpp>

#include "metel/meta.hpp"
#include <type_traits>

template <typename A, typename B>
inline constexpr bool same_v = std::is_same_v<A, B>;

using metel::typelist;

TEST("typelist") {
  struct A {};
  struct B {};
  struct C {};
  struct D {};

  using L = typelist<int, double, char>;
  static_assert(metel::is_typelist<L>::value);
  static_assert(!metel::is_typelist<void>::value);

  static_assert(L::contains<int>());
  static_assert(L::contains<double>());
  static_assert(L::contains<char>());
  static_assert(!L::contains<char&>());
  static_assert(!L::contains<const char>());
  static_assert(!L::contains<float>());
  static_assert(!L::contains<A>());
  static_assert(!L::contains<void>());

  static_assert(typelist<>{}.contains<int>() == false);
  static_assert(typelist<>{}.contains<void>() == false);

  static_assert(L::contains(std::type_identity<int>{}));
  static_assert(L::contains(std::type_identity<char>{}));
  static_assert(!L::contains(std::type_identity<float>{}));

  static_assert(L::contains<int>() == L::contains(std::type_identity<int>{}));
  static_assert(L::contains<float>() == L::contains(std::type_identity<float>{}));

  static_assert(L::contains_any(typelist<double, float>{}));
  static_assert(L::contains_any(typelist<int>{}));
  static_assert(!L::contains_any(typelist<float, A, B>{}));
  static_assert(!L::contains_any(typelist<>{}));

  static_assert(L::contains_all(typelist<int, char>{}));
  static_assert(L::contains_all(typelist<int, double, char>{}));
  static_assert(!L::contains_all(typelist<int, float>{}));
  static_assert(L::contains_all(typelist<>{}));

  static_assert(same_v<decltype(typelist<int, double>{} + typelist<char, float>{}),
                       typelist<int, double, char, float>>);

  static_assert(same_v<decltype(typelist<A>{} + typelist<B>{} + typelist<C>{}), typelist<A, B, C>>);

  static_assert(same_v<decltype(typelist<int>{} + typelist<int>{}), typelist<int, int>>);

  static_assert(same_v<decltype(typelist<int, double>{} + typelist<>{}), typelist<int, double>>);
  static_assert(same_v<decltype(typelist<>{} + typelist<int, double>{}), typelist<int, double>>);

  static_assert(same_v<decltype(L::merge(typelist<double, float>{})), typelist<int, double, char, float>>);

  static_assert(same_v<decltype(L::merge(typelist<int, double, char>{})), typelist<int, double, char>>);

  static_assert(same_v<decltype(L::merge(typelist<>{})), typelist<int, double, char>>);

  static_assert(same_v<decltype(typelist<>{}.merge(typelist<int, char>{})), typelist<int, char>>);

  static_assert(
      same_v<decltype(L::merge(typelist<short, float>{})), typelist<int, double, char, short, float>>);

  static_assert(
      same_v<decltype(L::merge(typelist<float, float>{})), typelist<int, double, char, float, float>>);

  static_assert(same_v<decltype(L::merge(L{})), typelist<int, double, char>>);

  static_assert(
      same_v<decltype(L{} | typelist<double, float>{}), decltype(L::merge(typelist<double, float>{}))>);

  static_assert(same_v<decltype(L{} | typelist<double, float>{}), typelist<int, double, char, float>>);

  static_assert(same_v<decltype(L{} | typelist<>{}), typelist<int, double, char>>);

  static_assert(same_v<decltype(typelist<A>{} | typelist<B>{} | typelist<C, A>{}), typelist<A, B, C>>);
}

MOKO3_MAIN;
