
// на 90% сгенерировано ллм

#include <moko3/moko3.hpp>

#include "metel/fixed_array.hpp"

#include <stdexcept>
#include <type_traits>
#include <utility>

using metel::fixed_array;

namespace {

// Тип, который считает живые экземпляры/копии/перемещения, чтобы можно
// было проверять и значения, и то, что конструкторы/деструкторы вызываются
// ровно нужное число раз (нет утечек и двойных разрушений).
struct Probe {
  int value = 0;

  static inline int alive = 0;
  static inline int copies = 0;
  static inline int moves = 0;

  static void reset() {
    alive = 0;
    copies = 0;
    moves = 0;
  }

  Probe() {
    ++alive;
  }
  explicit Probe(int v) : value(v) {
    ++alive;
  }
  Probe(const Probe& other) : value(other.value) {
    ++alive;
    ++copies;
  }
  Probe(Probe&& other) noexcept : value(other.value) {
    ++alive;
    ++moves;
  }
  Probe& operator=(const Probe& other) {
    value = other.value;
    return *this;
  }
  Probe& operator=(Probe&& other) noexcept {
    value = other.value;
    return *this;
  }
  ~Probe() {
    --alive;
  }
};

struct TwoArgs {
  int a, b;
  TwoArgs(int a_, int b_) : a(a_), b(b_) {
  }
};

struct Thrower {
  static inline int construct_count = 0;
  static inline int limit = -1;
  int value;
  explicit Thrower(int v) : value(v) {
    ++construct_count;
  }
  Thrower(const Thrower& other) : value(other.value) {
    ++construct_count;
    if (limit >= 0 && construct_count > limit)
      throw std::runtime_error("boom");
  }
  Thrower& operator=(const Thrower& other) = default;
};

// Аллокатор с идентичностью (id) и настраиваемой через Policy стратегией
// propagate_on_container_*, чтобы проверить, что fixed_array правильно
// (или, где выяснилось, что неправильно) реагирует на эти трейты.
struct PropagateAll {
  static constexpr bool copy = true, move = true, swap_ = true, always_equal = false;
};
struct PropagateNone {
  static constexpr bool copy = false, move = false, swap_ = false, always_equal = false;
};
struct AlwaysEqualNoPropagate {
  static constexpr bool copy = false, move = false, swap_ = false, always_equal = true;
};

template <typename T, typename Policy>
struct TrackingAllocator {
  using value_type = T;
  using propagate_on_container_copy_assignment = std::bool_constant<Policy::copy>;
  using propagate_on_container_move_assignment = std::bool_constant<Policy::move>;
  using propagate_on_container_swap = std::bool_constant<Policy::swap_>;
  using is_always_equal = std::bool_constant<Policy::always_equal>;

  int id = 0;
  static inline int allocations = 0;
  static inline int deallocations = 0;

  static void reset_counters() {
    allocations = 0;
    deallocations = 0;
  }

  TrackingAllocator() = default;
  explicit TrackingAllocator(int id_) : id(id_) {
  }
  template <typename U>
  TrackingAllocator(const TrackingAllocator<U, Policy>& other) noexcept : id(other.id) {
  }

  T* allocate(size_t count) {
    ++allocations;
    return static_cast<T*>(::operator new(count * sizeof(T)));
  }
  void deallocate(T* p, size_t) noexcept {
    ++deallocations;
    ::operator delete(p);
  }

  friend bool operator==(const TrackingAllocator& x, const TrackingAllocator& y) noexcept {
    return x.id == y.id;
  }
  friend bool operator!=(const TrackingAllocator& x, const TrackingAllocator& y) noexcept {
    return !(x == y);
  }
};

}  // namespace

TEST("default-constructed array is empty") {
  fixed_array<int> a;
  REQUIRE(a.size() == 0);
  REQUIRE(a.begin() == a.end());
  REQUIRE(a.data() == nullptr);
}

TEST("sized constructor default-constructs n elements") {
  Probe::reset();
  {
    fixed_array<Probe> a(5);
    REQUIRE(a.size() == 5);
    for (auto& p : a)
      REQUIRE(p.value == 0);
    REQUIRE(Probe::alive == 5);
  }
  REQUIRE(Probe::alive == 0);
}

TEST("sized constructor with n == 0 does not allocate") {
  fixed_array<int> a(0);
  REQUIRE(a.size() == 0);
  REQUIRE(a.data() == nullptr);
}

TEST("fill constructor forwards multiple arguments to each element") {
  fixed_array<TwoArgs> a(3, std::in_place, 1, 2);
  REQUIRE(a.size() == 3);
  for (auto& x : a) {
    REQUIRE(x.a == 1);
    REQUIRE(x.b == 2);
  }
}

TEST("fill constructor with n == 0 constructs nothing and does not allocate") {
  Probe::reset();
  fixed_array<Probe> a(0, std::in_place, 99);
  REQUIRE(a.size() == 0);
  REQUIRE(a.data() == nullptr);
  REQUIRE(Probe::alive == 0);
}

TEST("fill constructor copies the argument into every element, not moves it") {
  Probe::reset();
  fixed_array<Probe> a(4, std::in_place, Probe(7));
  REQUIRE(Probe::copies == 4);  // аргумент используется 4 раза, поэтому это копии
  for (auto& p : a)
    REQUIRE(p.value == 7);
}

TEST("copy constructor deep-copies and is independent from the source") {
  Probe::reset();
  fixed_array<Probe> a(3, std::in_place, 10);
  fixed_array<Probe> b(a);

  REQUIRE(b.size() == a.size());
  REQUIRE(b.data() != a.data());
  for (size_t i = 0; i < a.size(); ++i)
    REQUIRE(b[i].value == a[i].value);

  b[0].value = 999;
  REQUIRE(a[0].value == 10);  // изменение копии не влияет на оригинал
}

TEST("copy assignment replaces contents and is self-assignment safe") {
  Probe::reset();
  fixed_array<Probe> a(2, std::in_place, 1);
  fixed_array<Probe> b(5, std::in_place, 2);

  b = a;
  REQUIRE(b.size() == 2);
  REQUIRE(b[0].value == 1);
  REQUIRE(b[1].value == 1);

  a = a;  // самоприсваивание не должно ничего ломать
  REQUIRE(a.size() == 2);
  REQUIRE(a[0].value == 1);
}

TEST("copy assignment provides strong guarantee on throwing copy ctor") {
  fixed_array<Thrower> source(4, std::in_place, 0);
  for (size_t i = 0; i < source.size(); ++i)
    source[i] = Thrower(int(i));

  fixed_array<Thrower> target(2, std::in_place, 0);
  Thrower::construct_count = 0;
  Thrower::limit = 2;  // упадёт на копировании третьего элемента

  try {
    target = source;
    FAIL();
  } catch (std::runtime_error&) {
    // OK
  }

  // старое содержимое target должно остаться нетронутым (строгая гарантия)
  REQUIRE(target.size() == 2);
  REQUIRE(target[0].value == 0);
  REQUIRE(target[1].value == 0);

  Thrower::limit = -1;
}

TEST("move constructor steals storage and empties the source") {
  fixed_array<int> a(3, std::in_place, 5);
  int* original_ptr = a.data();

  fixed_array<int> b(std::move(a));
  REQUIRE(b.size() == 3);
  REQUIRE(b.data() == original_ptr);
  REQUIRE(b[0] == 5);

  REQUIRE(a.size() == 0);
  REQUIRE(a.data() == nullptr);
}

TEST("move assignment steals storage and destroys previous contents") {
  Probe::reset();
  fixed_array<Probe> a(2, std::in_place, 1);
  fixed_array<Probe> b(5, std::in_place, 2);

  int* unused = nullptr;
  (void)unused;
  REQUIRE(Probe::alive == 7);

  b = std::move(a);
  REQUIRE(b.size() == 2);
  REQUIRE(b[0].value == 1);
  REQUIRE(Probe::alive == 2);  // 5 старых элементов b разрушены

  REQUIRE(a.size() == 0);
  REQUIRE(a.data() == nullptr);
}

TEST("move assignment is self-move-assignment safe") {
  fixed_array<Probe> a(3, std::in_place, 7);
  Probe* ptr = a.data();

  auto move_into = [](fixed_array<Probe>& dst, fixed_array<Probe>& src) { dst = std::move(src); };
  move_into(a, a);

  REQUIRE(a.size() == 3);
  REQUIRE(a.data() == ptr);
  for (auto& p : a)
    REQUIRE(p.value == 7);
}

TEST("member swap and free swap exchange size, data and contents") {
  fixed_array<int> a(2, std::in_place, 1);
  fixed_array<int> b(4, std::in_place, 2);
  int* a_ptr = a.data();
  int* b_ptr = b.data();

  a.swap(b);
  REQUIRE(a.size() == 4);
  REQUIRE(b.size() == 2);
  REQUIRE(a.data() == b_ptr);
  REQUIRE(b.data() == a_ptr);
  REQUIRE(a[0] == 2);
  REQUIRE(b[0] == 1);

  swap(a, b);  // ADL free-функция должна вернуть исходное состояние
  REQUIRE(a.size() == 2);
  REQUIRE(b.size() == 4);
}

TEST("const data()/begin()/end() are const-correct") {
  fixed_array<int> a(3, std::in_place, 1);
  const fixed_array<int>& ca = a;

  static_assert(std::is_same_v<decltype(ca.data()), const int*>);
  static_assert(std::is_same_v<decltype(ca.begin()), const int*>);
  static_assert(std::is_same_v<decltype(ca.end()), const int*>);
  static_assert(std::is_same_v<decltype(a.data()), int*>);

  REQUIRE(ca.data() == a.data());
}

TEST("select_on_container_copy_construction propagates allocator identity on copy ctor") {
  using Alloc = TrackingAllocator<Probe, PropagateNone>;
  fixed_array<Probe, Alloc> a(3, Alloc(5));
  for (auto& p : a)
    p.value = 1;

  fixed_array<Probe, Alloc> b(a);
  REQUIRE(b.get_alloc().id == 5);
  REQUIRE(b[0].value == 1);
}

TEST("copy assignment respects propagate_on_container_copy_assignment") {
  using AllocProp = TrackingAllocator<Probe, PropagateAll>;
  using AllocNoProp = TrackingAllocator<Probe, PropagateNone>;

  SECTION("propagating allocator is adopted on copy assignment") {
    fixed_array<Probe, AllocProp> a(2, AllocProp(1));
    for (auto& p : a)
      p.value = 1;
    fixed_array<Probe, AllocProp> b(2, AllocProp(2));
    for (auto& p : b)
      p.value = 2;

    b = a;
    REQUIRE(b.get_alloc().id == 1);
    REQUIRE(b[0].value == 1);
  }

  SECTION("non-propagating allocator is kept on copy assignment") {
    fixed_array<Probe, AllocNoProp> a(2, AllocNoProp(1));
    for (auto& p : a)
      p.value = 1;
    fixed_array<Probe, AllocNoProp> b(2, AllocNoProp(2));
    for (auto& p : b)
      p.value = 2;

    b = a;
    REQUIRE(b.get_alloc().id == 2);  // аллокатор b не поменялся
    REQUIRE(b[0].value == 1);        // но данные всё равно скопированы верно
  }
}

TEST("move assignment adopts allocator when propagate_on_container_move_assignment is true") {
  using Alloc = TrackingAllocator<Probe, PropagateAll>;
  fixed_array<Probe, Alloc> a(2, Alloc(1));
  for (auto& p : a)
    p.value = 1;
  fixed_array<Probe, Alloc> b(2, Alloc(2));
  Probe* a_ptr = a.data();

  b = std::move(a);

  REQUIRE(b.get_alloc().id == 1);  // аллокатор источника распространился
  REQUIRE(b.data() == a_ptr);      // и буфер именно украден, не пересоздан
  REQUIRE(b[0].value == 1);
  REQUIRE(a.size() == 0);
  REQUIRE(a.data() == nullptr);
}

TEST("move assignment steals the buffer when is_always_equal is true, without propagating alloc") {
  using Alloc = TrackingAllocator<Probe, AlwaysEqualNoPropagate>;
  fixed_array<Probe, Alloc> a(2, Alloc(1));
  for (auto& p : a)
    p.value = 1;
  fixed_array<Probe, Alloc> b(2, Alloc(2));
  Probe* a_ptr = a.data();

  b = std::move(a);

  REQUIRE(b.data() == a_ptr);      // украли буфер...
  REQUIRE(b.get_alloc().id == 2);  // ...но свой allocator не поменяли (POCMA=false)
  REQUIRE(b[0].value == 1);
  REQUIRE(a.size() == 0);
  REQUIRE(a.data() == nullptr);
}

TEST("move assignment steals the buffer when allocators compare equal at runtime") {
  using Alloc = TrackingAllocator<Probe, PropagateNone>;
  fixed_array<Probe, Alloc> a(2, Alloc(7));
  for (auto& p : a)
    p.value = 1;
  fixed_array<Probe, Alloc> b(2, Alloc(7));  // тот же id => равны
  Probe* a_ptr = a.data();

  b = std::move(a);

  REQUIRE(b.data() == a_ptr);
  REQUIRE(b[0].value == 1);
  REQUIRE(a.size() == 0);
  REQUIRE(a.data() == nullptr);
}

TEST("move assignment falls back to element-wise move for unequal non-propagating allocators") {
  Probe::reset();
  using Alloc = TrackingAllocator<Probe, PropagateNone>;
  fixed_array<Probe, Alloc> a(2, Alloc(1));
  for (auto& p : a)
    p.value = 1;
  fixed_array<Probe, Alloc> b(2, Alloc(2));
  for (auto& p : b)
    p.value = 2;
  Probe* a_ptr = a.data();
  REQUIRE(Probe::alive == 4);

  b = std::move(a);

  // Буфер не украден — b выделил новую память своим (не пропагированным)
  // аллокатором и поэлементно переместил в неё значения.
  REQUIRE(b.data() != a_ptr);
  REQUIRE(b.get_alloc().id == 2);  // аллокатор b не поменялся
  REQUIRE(b[0].value == 1);
  REQUIRE(b[1].value == 1);

  // a не опустошён: он по-прежнему владеет своим (перемещённым-из) буфером
  // и сам корректно освободит его в деструкторе.
  REQUIRE(a.size() == 2);
  REQUIRE(a.data() == a_ptr);

  REQUIRE(Probe::moves == 2);  // именно move, не copy
  REQUIRE(Probe::alive == 4);  // ни утечки, ни двойного разрушения
}

TEST("swap propagates allocators when propagate_on_container_swap is true") {
  using Alloc = TrackingAllocator<Probe, PropagateAll>;
  fixed_array<Probe, Alloc> a(2, Alloc(1));
  fixed_array<Probe, Alloc> b(3, Alloc(2));

  a.swap(b);
  REQUIRE(a.get_alloc().id == 2);
  REQUIRE(b.get_alloc().id == 1);
  REQUIRE(a.size() == 3);
  REQUIRE(b.size() == 2);
}

TEST("swap with non-propagating but equal allocators works fine") {
  using Alloc = TrackingAllocator<Probe, PropagateNone>;
  fixed_array<Probe, Alloc> a(2, Alloc(7));
  fixed_array<Probe, Alloc> b(3, Alloc(7));  // тот же id => allocator-ы равны

  a.swap(b);
  REQUIRE(a.size() == 3);
  REQUIRE(b.size() == 2);
  REQUIRE(a.get_alloc().id == 7);
  REQUIRE(b.get_alloc().id == 7);
}

TEST("explicit prevents implicit conversion from size_t") {
  // Компилируется только если конструкторы действительно explicit:
  // fixed_array<int> a = 5; // не должно компилироваться
  static_assert(!std::is_convertible_v<size_t, fixed_array<int>>);
}

TEST("operator[] round-trips values written through non-const access") {
  fixed_array<int> a(4, std::in_place, 0);
  for (size_t i = 0; i < a.size(); ++i)
    a[i] = int(i * i);
  const fixed_array<int>& ca = a;
  for (size_t i = 0; i < a.size(); ++i)
    REQUIRE(ca[i] == int(i * i));
}

MOKO3_MAIN;
