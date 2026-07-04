#pragma once

#include <compare>
#include <memory>
#include <type_traits>
#include <utility>
#include <cassert>
#include <optional>
#include <initializer_list>

#include <zal/zal.hpp>

#include "metel/macro.hpp"
#include "metel/meta.hpp"

namespace metel {

// optional, но позволяет быть T неполным типом
template <typename T>
struct METEL_TRIVIAL_ABI box {
  static_assert(is_decayed_v<T>);
  using value_type = T;

 private:
  T* ptr = nullptr;

 public:
  box() = default;

  constexpr box(std::nullptr_t) noexcept {
  }
  constexpr box(std::nullopt_t) noexcept : box(nullptr) {
  }
  constexpr ~box() {
    reset();
  }

  constexpr box(const box& other)
    requires(std::is_copy_constructible_v<T>)
  {
    if (!other.ptr)
      return;
    ptr = new T(*other.ptr);
  }
  constexpr box(box&& other) noexcept : ptr(std::exchange(other.ptr, nullptr)) {
  }

  constexpr box& operator=(const box& other)
    requires(std::is_copy_constructible_v<T>)
  {
    *this = box(other);
    return *this;
  }

  constexpr box& operator=(box&& other) noexcept {
    swap(other);
    return *this;
  }

  constexpr box& operator=(std::nullptr_t) noexcept {
    reset();
    return *this;
  }
  constexpr box& operator=(std::nullopt_t) noexcept {
    return *this = nullptr;
  }

  template <typename U = T>
  constexpr box& operator=(U&& v)
    requires(!std::is_same_v<box, std::decay_t<U>> && std::is_constructible_v<T, U &&>)
  {
    emplace(std::forward<U>(v));
    return *this;
  }

  constexpr void swap(box& other) noexcept {
    std::swap(ptr, other.ptr);
  }
  constexpr friend void swap(box& a, box& b) noexcept {
    a.swap(b);
  }

  template <typename U = T>
    requires(!std::is_same_v<box, std::decay_t<U>> && std::is_constructible_v<T, U &&>)
  constexpr explicit(!std::is_convertible_v<U&&, T>) box(U&& v) : ptr(new T(std::forward<U>(v))) {
  }

  template <typename U, typename... Args>
  constexpr value_type& emplace(std::initializer_list<U> list, Args&&... args) {
    // avoid recursion
    return emplace<std::initializer_list<U>, Args...>(std::move(list), std::forward<Args>(args)...);
  }

  // if exception thrown from T constructor, then has_value() == false
  template <typename... Args>
  constexpr T& emplace(Args&&... args) {
    if (!ptr) {
      ptr = new T(std::forward<Args>(args)...);
      return *ptr;
    }
    // reuse memory
    if constexpr (std::is_nothrow_constructible_v<T, Args&&...>) {
      std::destroy_at(ptr);
      std::construct_at(ptr, std::forward<Args>(args)...);
      return *ptr;
    } else if constexpr (std::is_nothrow_default_constructible_v<T>) {
      std::destroy_at(ptr);
      on_scope_failure(freemem) {
        // avoid calling destructor on empty memory by 'delete'
        std::construct_at(ptr);
        reset();
      };
      std::construct_at(ptr, std::forward<Args>(args)...);
      freemem.no_longer_needed();
      return *ptr;
    } else {
      reset();
      ptr = new T(std::forward<Args>(args)...);
      return *ptr;
    }
  }

  [[nodiscard]] constexpr bool has_value() const noexcept {
    return ptr != nullptr;
  }

  constexpr explicit operator bool() const noexcept {
    return has_value();
  }

  constexpr void reset() noexcept {
    static_assert(sizeof(T));
    if (ptr) {
      delete ptr;
      ptr = nullptr;
    }
  }

  template <typename U>
  constexpr T value_or(U&& v) & {
    return has_value() ? T(**this) : static_cast<T>(std::forward<U>(v));
  }
  template <typename U>
  constexpr T value_or(U&& v) const& {
    return has_value() ? T(**this) : static_cast<T>(std::forward<U>(v));
  }

  template <typename U>
  constexpr T value_or(U&& v) && {
    return has_value() ? T(std::move(**this)) : static_cast<T>(std::forward<U>(v));
  }
  template <typename U>
  constexpr T value_or(U&& v) const&& {
    return has_value() ? T(std::move(**this)) : static_cast<T>(std::forward<U>(v));
  }

  constexpr T* operator->() noexcept {
    return ptr;
  }
  constexpr const T* operator->() const noexcept {
    return ptr;
  }

  constexpr T& value() & {
    if (!has_value())
      throw std::bad_optional_access();
    return **this;
  }

  constexpr T&& value() && {
    if (!has_value())
      throw std::bad_optional_access();
    return std::move(**this);
  }

  constexpr const T& value() const& {
    if (!has_value())
      throw std::bad_optional_access();
    return **this;
  }

  constexpr const T&& value() const&& {
    if (!has_value())
      throw std::bad_optional_access();
    return std::move(**this);
  }

  constexpr T& operator*() & noexcept {
    assert(ptr);
    return *ptr;
  }
  constexpr const T& operator*() const& noexcept {
    assert(ptr);
    return *ptr;
  }
  constexpr T&& operator*() && noexcept {
    assert(ptr);
    return std::move(*ptr);
  }
  constexpr const T&& operator*() const&& noexcept {
    assert(ptr);
    return std::move(*ptr);
  }

  // assumes 'ptr' may be released with 'delete'
  static constexpr box from_raw(T* ptr) noexcept {
    box b;
    b.ptr = ptr;
    return b;
  }

  [[nodiscard]] constexpr T* release() noexcept {
    return std::exchange(ptr, nullptr);
  }

  constexpr bool operator==(std::nullptr_t) const noexcept {
    return !has_value();
  }
  constexpr bool operator==(std::nullopt_t) const noexcept {
    return *this == nullptr;
  }

  // noexcept на операторах сравнения безусловный, так как бросающее исключение сравнение это безумие
  // а проверка noexcept добавляет нетривиальных ошибок при использовании из-за возникающих рекурсий и тд
  friend constexpr auto operator<=>(const box& l, const box& r) noexcept
    requires(std::three_way_comparable<T>)
  {
    // compare_three_way_result_t спрятано внутрь, чтобы не обваливать ещё при проверке в возвращаемом типе
    return std::compare_three_way_result_t<T>(l && r ? *l <=> *r : bool(l) <=> bool(r));
  }

  friend constexpr bool operator==(const box& l, const box& r) noexcept
    requires(std::equality_comparable<T>)
  {
    return l && r ? *l == *r : !l && !r;
  }

  // для удобного join на ренжах из box
  constexpr T* begin() noexcept {
    return has_value() ? ptr : nullptr;
  }
  constexpr const T* begin() const noexcept {
    return has_value() ? ptr : nullptr;
  }
  constexpr T* end() noexcept {
    return has_value() ? ptr + 1 : nullptr;
  }
  constexpr const T* end() const noexcept {
    return has_value() ? ptr + 1 : nullptr;
  }
};

template <typename T>
box(T&&) -> box<std::decay_t<T>>;

}  // namespace metel
