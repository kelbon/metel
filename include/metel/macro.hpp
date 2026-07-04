#pragma once

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
