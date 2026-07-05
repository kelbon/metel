#pragma once

#include <utility>

#include "metel/macro.hpp"

namespace metel {

template <typename T>
inline constexpr bool is_decayed_v = std::is_same_v<T, std::decay_t<T>>;

enum { VISIT_INDEX_MAX = 128 - 1 };
// switch 'i' up to 64 for better code generatrion
// 'F' should be like [] <size_t I> {}
// Max - index until 'f' will be instanciated, assumes i <= MAX
template <size_t Max>
constexpr decltype(auto) visit_index(auto&& f, size_t i) {
  static_assert(Max <= VISIT_INDEX_MAX);
  assert(i <= VISIT_INDEX_MAX);
  switch (i) {
#define $METEL_SWITCH_CASE(INDEX)            \
  case INDEX: {                              \
    if constexpr (INDEX <= Max) {            \
      return f.template operator()<INDEX>(); \
    } else {                                 \
      metel::unreachable();                  \
    }                                        \
  }
#define $METEL_SWITCH_CASE4(I) \
  $METEL_SWITCH_CASE(I) $METEL_SWITCH_CASE(I + 1) $METEL_SWITCH_CASE(I + 2) $METEL_SWITCH_CASE(I + 3)
#define $METEL_SWITCH_CASE8(I) $METEL_SWITCH_CASE4(I) $METEL_SWITCH_CASE4(I + 4)
#define $METEL_SWITCH_CASE16(I) $METEL_SWITCH_CASE8(I) $METEL_SWITCH_CASE8(I + 8)
#define $METEL_SWITCH_CASE32(I) $METEL_SWITCH_CASE16(I) $METEL_SWITCH_CASE16(I + 16)
#define $METEL_SWITCH_CASE64(I) $METEL_SWITCH_CASE32(I) $METEL_SWITCH_CASE32(I + 32)
#define $METEL_SWITCH_CASE128(I) $METEL_SWITCH_CASE64(I) $METEL_SWITCH_CASE64(I + 64)
    $METEL_SWITCH_CASE128(0);
  }  // end switch
  metel::unreachable();
#undef $METEL_SWITCH_CASE
#undef $METEL_SWITCH_CASE4
#undef $METEL_SWITCH_CASE8
#undef $METEL_SWITCH_CASE16
#undef $METEL_SWITCH_CASE32
#undef $METEL_SWITCH_CASE64
}

template <typename... Foos>
struct matcher : Foos... {
  using Foos::operator()...;
};

template <typename... Foos>
matcher(Foos...) -> matcher<Foos...>;

// same as 'matcher', but casts every returned value to type Ret,
// so you dont need duplicate return type on every function
template <typename Ret, typename... Foos>
constexpr auto matcher_r(Foos&&... foos) noexcept(
    (... && std::is_nothrow_constructible_v<std::remove_cvref_t<Foos>, Foos&&>)) {
  return [m = matcher{std::forward<Foos>(foos)...}](auto&&... args) -> Ret {
    // - uses RVO, no move
    // - do not uses std::invoke for better compilation speed/errors
    // and no one literaly needed to pass member pointers here
    return static_cast<Ret>(m(std::forward<decltype(args)>(args)...));
  };
}

consteval size_t log2_constexpr(size_t n) {
  return n == 1 ? 0 : log2_constexpr(n / 2) + 1;
}

template <typename T, typename... Ts>
consteval size_t find_first() {
  bool same[]{std::same_as<T, Ts>...};
  for (bool& t : same)
    if (t)
      return &t - &same[0];
  return size_t(-1);
}

template <typename T, typename... Ts>
consteval bool contains_type() {
  return find_first<T, Ts...>() != -1;
}

template <size_t I, typename... Types>
struct element_at {
  // no type, empty Types
};

template <typename Head, typename... Tail>
struct element_at<0, Head, Tail...> {
  using type = Head;
};

template <size_t I, typename Head, typename... Tail>
struct element_at<I, Head, Tail...> {
  using type = typename element_at<I - 1, Tail...>::type;
};

template <size_t I, typename... Types>
using element_at_t = typename element_at<I, Types...>::type;

template <typename... Ts>
consteval bool is_unique_types() {
  size_t is[] = {find_first<Ts, Ts...>()..., size_t(-1) /* avoid empty array */};
  auto count = [&is](size_t val) {
    size_t c = 0;
    for (size_t x : is)
      c += val == x;
    return c;
  };
  return ((count(find_first<Ts, Ts...>()) == 1) && ...);
}

template <typename T, typename... Types>
concept oneof_types = (std::same_as<T, Types> || ...);

template <typename... Types>
struct typelist {
  static constexpr inline size_t size = sizeof...(Types);

  template <typename T>
  static constexpr bool contains() {
    return (std::is_same_v<Types, T> || ...);
  }
  template <typename T>
  static constexpr bool contains(std::type_identity<T>) {
    return contains_type<T, Types...>();
  }
  template <typename... Ts>
  static constexpr bool contains_any(typelist<Ts...>) {
    return (contains<Ts>() || ...);
  }
  template <typename... Ts>
  static constexpr bool contains_all(typelist<Ts...>) {
    return (contains<Ts>() && ...);
  }

  template <typename... Ts>
  static constexpr auto merge(typelist<Ts...>) {
    return typelist<Types...>{} +
           (typelist<>{} + ... + std::conditional_t<contains<Ts>(), typelist<>, typelist<Ts>>{});
  }

  template <typename... Ts>
  constexpr auto operator|(typelist<Ts...> l) const noexcept {
    return merge(l);
  }

  template <typename... Ts>
  constexpr auto operator+(typelist<Ts...>) const noexcept -> typelist<Types..., Ts...> {
    return {};
  }

  template <typename... Ts>
  constexpr bool operator==(const typelist<Ts...>&) const noexcept {
    return std::is_same_v<typelist<Ts...>, typelist<Types...>>;
  }
};

template <typename>
struct is_typelist : std::false_type {};

template <typename... Ts>
struct is_typelist<typelist<Ts...>> : std::true_type {};

}  // namespace metel
