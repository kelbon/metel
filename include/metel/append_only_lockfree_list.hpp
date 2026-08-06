#pragma once

#include <atomic>
#include <cassert>
#include <iterator>
#include <utility>

namespace metel {

template <typename T>
struct append_only_lockfree_list_node {
  std::atomic<append_only_lockfree_list_node*> next = nullptr;
  T value = T{};

  static_assert(decltype(next)::is_always_lock_free);

  append_only_lockfree_list_node* find_last_node() noexcept {
    auto* prev = next.load(std::memory_order::acquire);
    if (!prev)
      return this;
    for (;;) {
      auto* n = prev->next.load(std::memory_order::acquire);
      if (!n)
        return prev;
      prev = n;
    }
  }

  // 'x' может быть нуллптр (0 нод), одной нодой (next == nullptr) или целым списком (next != nullptr)
  // Note: нужно следить чтобы `x` или ноды за ним не был частью этого же списка
  void attach_after_last(append_only_lockfree_list_node* x) noexcept {
    if (!x)
      return;
    auto* l = find_last_node();
    append_only_lockfree_list_node* lnext = nullptr;
    // strong гарантирует что не будет провала без изменения lnext на что-то отличное от nullptr
    while (!l->next.compare_exchange_strong(lnext, x, std::memory_order::acq_rel)) {
      l = lnext->find_last_node();
      lnext = nullptr;
    }
  }

  // посещает все ноды начиная с этой
  void visit_nodes(auto&& vtor) {
    auto* n = this;
    while (n) {
      // кеш до функции которая может удалить ноду
      auto* _next = n->next.load(std::memory_order::acquire);
      vtor(n);
      n = _next;
    }
  }
};

// локфри список
// параллельно элементы в список можно только добавлять
// пользователь сам должен гарантировать, что во время вызова
// деструктора нет параллельного использования
template <typename T>
struct append_only_lockfree_list {
  using node_type = append_only_lockfree_list_node<T>;

 private:
  std::atomic<node_type*> top = nullptr;

 public:
  // дефолт конструктор создаёт end iterator
  struct iterator {
    node_type* cur = nullptr;

    using value_type = T;
    using reference = T&;
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;

    node_type* node_ptr() const noexcept {
      return cur;
    }

    reference operator*() const noexcept {
      return cur->value;
    }
    pointer operator->() const noexcept {
      return std::addressof(cur->value);
    }
    iterator& operator++() noexcept {
      assert(cur);
      cur = cur->next.load(std::memory_order::acquire);
      return *this;
    }
    iterator operator++(int) noexcept {
      auto i = *this;
      ++*this;
      return i;
    }
    bool operator==(std::default_sentinel_t) const noexcept {
      return cur == nullptr;
    }
    bool operator==(const iterator& other) const noexcept {
      return cur == other.cur;
    }
  };

  append_only_lockfree_list() = default;

  append_only_lockfree_list(append_only_lockfree_list&&) = delete;
  void operator=(append_only_lockfree_list&&) = delete;

  ~append_only_lockfree_list() {
    foreach_node([](auto* n) { delete n; });
  }

  // есть вероятность не увидеть некоторые значения, если параллельно вызываются push_back/push_back_list
  iterator begin() noexcept {
    return iterator(top.load(std::memory_order::acquire));
  }

  static std::default_sentinel_t end() noexcept {
    return std::default_sentinel;
  }

  // для поддержки старых алгоритмов
  static iterator end_iterator() noexcept {
    return iterator{};
  }

  // удаляет ноды для значения которых cond(value) == true
  // pre: никто не использует *this параллельно
  void erase_if(auto&& cond) {
    static_assert(noexcept(cond(std::declval<T>())));
    node_type* t = top.load(std::memory_order::acquire);
    node_type* new_top = nullptr;
    node_type* prev = nullptr;
    while (t) {
      auto* next = t->next.load(std::memory_order::acquire);
      if (cond(std::as_const(t->value))) {
        if (prev)
          prev->next.store(next, std::memory_order::release);
        delete t;
      } else {
        if (!new_top)
          new_top = t;
        prev = t;
      }
      t = next;
    }
    top.store(new_top, std::memory_order::release);
  }

  // возвращает итератор на только что вставленную ноду
  // O(N)
  template <typename... Args>
  iterator push_back(Args&&... args) {
    node_type* newnode = new node_type(nullptr, T(std::forward<Args>(args)...));
    node_type* t = top.load(std::memory_order::acquire);
    if (!t) {
      if (top.compare_exchange_strong(t, newnode, std::memory_order::acq_rel))
        return iterator(newnode);
    }
    t->attach_after_last(newnode);
    return iterator(newnode);
  }

  // то же что и push_back, но  'hint' помогает быстрее найти последнюю ноду
  // pre: hint != end(), hint из этого же списка
  template <typename... Args>
  iterator push_back_hint(iterator hint, Args&&... args) {
    assert(hint != end());
    node_type* newnode = new node_type(nullptr, T(std::forward<Args>(args)...));
    hint.node_ptr()->attach_after_last(newnode);
    return iterator(newnode);
  }

  // pre: `list` никто параллельно не использует и не вызывает у него методы
  void push_back_list(append_only_lockfree_list&& list) noexcept {
    node_type* othertop = list.top.exchange(nullptr, std::memory_order::acq_rel);
    if (!othertop)
      return;
    node_type* t = top.load(std::memory_order::acquire);
    if (!t) {
      if (top.compare_exchange_strong(t, othertop, std::memory_order::acq_rel))
        return;
    }
    t->attach_after_last(othertop);
  }

  // Note: результат может измениться сразу после вызова функции, так как возможны параллельные push_back
  bool empty() const noexcept {
    return top.load(std::memory_order::acquire) == nullptr;
  }

  // есть вероятность не увидеть некоторые ноды, если параллельно вызываются push_back/push_back_list
  void foreach_node(auto&& vtor) {
    auto* t = top.load(std::memory_order::acquire);
    if (t)
      t->visit_nodes(vtor);
  }
};

}  // namespace metel
