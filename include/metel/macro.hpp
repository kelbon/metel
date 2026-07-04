#pragma once

#include <cassert>

#ifdef _MSC_VER
  #define METEL_MSVC_EBO __declspec(empty_bases)
#else
  #define METEL_MSVC_EBO
#endif

#ifdef __clang__
  #define METEL_TRIVIAL_ABI [[clang::trivial_abi]]
#else
  #define METEL_TRIVIAL_ABI
#endif

namespace metel {

[[noreturn]] inline void unreachable() noexcept {
  assert(false);
#if defined(__cpp_lib_unreachable) && __cpp_lib_unreachable >= 202202L
  std::unreachable();
#elif defined(_MSC_VER) && !defined(__clang__)
  __assume(false);
#elif defined(__GNUC__) || defined(__clang__)
  __builtin_unreachable();
#else
  ::metel::unreachable();
#endif
}

}  // namespace metel
