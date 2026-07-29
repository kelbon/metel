#pragma once

#include <cassert>
#include <memory>
#include <utility>

#include <zal/zal.hpp>

#include "metel/meta.hpp"

namespace metel {

// массив фиксированного размера хорошо подходящий для ситуации когда нужен vector<non_movable_type>
template <typename T, typename Alloc = std::allocator<T>>
struct fixed_array {
 private:
  T* arr = nullptr;
  size_t n = 0;
  using alloc_type = std::allocator_traits<Alloc>::template rebind_alloc<T>;
  using alloc_traits = std::allocator_traits<alloc_type>;
  ZAL_NO_UNIQUE_ADDRESS alloc_type alloc;

 public:
  static_assert(metel::is_decayed_v<T>);

  fixed_array() = default;

  fixed_array(const fixed_array& other)
      : n(other.n), alloc(alloc_traits::select_on_container_copy_construction(other.alloc)) {
    if (n == 0)
      return;
    arr = alloc_traits::allocate(alloc, n);
    on_scope_failure(dealloc) {
      alloc_traits::deallocate(alloc, arr, n);
    };
    std::uninitialized_copy_n(other.arr, n, arr);
    dealloc.no_longer_needed();
  }

  fixed_array& operator=(const fixed_array& other) {
    if (this == &other)
      return *this;

    alloc_type new_alloc = alloc_traits::propagate_on_container_copy_assignment::value ? other.alloc : alloc;

    T* new_arr = nullptr;
    if (other.n != 0) {
      new_arr = alloc_traits::allocate(new_alloc, other.n);
      on_scope_failure(dealloc) {
        alloc_traits::deallocate(new_alloc, new_arr, other.n);
      };
      std::uninitialized_copy_n(other.arr, other.n, new_arr);
      dealloc.no_longer_needed();
    }

    clear();
    arr = new_arr;
    n = other.n;
    alloc = std::move(new_alloc);
    return *this;
  }

  fixed_array(fixed_array&& other) noexcept
      : arr(std::exchange(other.arr, nullptr)), n(std::exchange(other.n, 0)), alloc(std::move(other.alloc)) {
  }
  fixed_array& operator=(fixed_array&& other) noexcept(
      alloc_traits::propagate_on_container_move_assignment::value || alloc_traits::is_always_equal::value) {
    if (this == &other)
      return *this;

    if constexpr (alloc_traits::propagate_on_container_move_assignment::value ||
                  alloc_traits::is_always_equal::value) {
      clear();
      if constexpr (alloc_traits::propagate_on_container_move_assignment::value)
        alloc = std::move(other.alloc);
      arr = std::exchange(other.arr, nullptr);
      n = std::exchange(other.n, 0);
    } else {
      if (alloc == other.alloc) {
        clear();
        arr = std::exchange(other.arr, nullptr);
        n = std::exchange(other.n, 0);
      } else {
        T* new_arr = nullptr;
        size_t i = 0;
        if (other.n != 0) {
          new_arr = alloc_traits::allocate(alloc, other.n);
          on_scope_failure(dealloc) {
            std::destroy_n(new_arr, i);
            alloc_traits::deallocate(alloc, new_arr, other.n);
          };
          for (; i < other.n; ++i)
            std::construct_at(new_arr + i, std::move(other.arr[i]));
          dealloc.no_longer_needed();
        }
        clear();
        arr = new_arr;
        n = other.n;
        // other намеренно не трогаем: он остаётся владельцем своего буфера
        // (теперь из перемещённых-из элементов) и сам корректно его освободит.
      }
    }
    return *this;
  }

  void swap(fixed_array& other) noexcept {
    using std::swap;
    if constexpr (alloc_traits::propagate_on_container_swap::value) {
      swap(alloc, other.alloc);
    } else {
      assert(alloc == other.alloc);
    }
    swap(arr, other.arr);
    swap(n, other.n);
  }

  friend void swap(fixed_array& a, fixed_array& b) noexcept {
    a.swap(b);
  }

  explicit fixed_array(size_t n, alloc_type a = alloc_type{}) : n(n), alloc(std::move(a)) {
    if (n == 0)
      return;
    arr = alloc_traits::allocate(alloc, n);
    on_scope_failure(dealloc) {
      alloc_traits::deallocate(alloc, arr, n);
    };
    std::uninitialized_default_construct_n(arr, n);
    dealloc.no_longer_needed();
  }

  // передаёт аргументы каждому из 'n' элементов
  fixed_array(size_t n, std::in_place_t, auto&&... args) : n(n) {
    if (n == 0)
      return;
    arr = alloc_traits::allocate(alloc, n);
    size_t i = 0;
    on_scope_failure(dealloc) {
      std::destroy_n(arr, i);
      alloc_traits::deallocate(alloc, arr, n);
    };
    for (; i < n; ++i)
      std::construct_at(arr + i, args...);
    dealloc.no_longer_needed();
  }

  const alloc_type& get_alloc() const noexcept {
    return alloc;
  }

  T* data() noexcept {
    return arr;
  }

  const T* data() const noexcept {
    return arr;
  }

  size_t size() const noexcept {
    return n;
  }

  T* begin() noexcept {
    return arr;
  }

  T* end() noexcept {
    return arr + n;
  }

  const T* begin() const noexcept {
    return arr;
  }

  const T* end() const noexcept {
    return arr + n;
  }

  T& operator[](size_t i) noexcept {
    assert(arr);
    assert(i < n);
    return arr[i];
  }

  const T& operator[](size_t i) const noexcept {
    assert(arr);
    assert(i < n);
    return arr[i];
  }

  void clear() {
    if (arr) {
      std::destroy_n(arr, n);
      // assume noexcept
      alloc_traits::deallocate(alloc, arr, n);
      arr = nullptr;
    }
  }

  ~fixed_array() {
    clear();
  }
};

}  // namespace metel
