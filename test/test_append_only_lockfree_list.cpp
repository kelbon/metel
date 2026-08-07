#include <moko3/moko3.hpp>
#include "metel/append_only_lockfree_list.hpp"

#include <atomic>
#include <numeric>
#include <set>
#include <string>
#include <thread>
#include <vector>

using metel::append_only_lockfree_list;

namespace {

template <typename T>
std::vector<T> collect(append_only_lockfree_list<T>& list) {
  std::vector<T> out;
  for (auto it = list.begin(); it != append_only_lockfree_list<T>::end(); ++it)
    out.push_back(*it);
  return out;
}

struct MoveOnly {
  int value = 0;
  static inline std::atomic<int> alive_count{0};

  MoveOnly() {
    ++alive_count;
  }
  explicit MoveOnly(int v) : value(v) {
    ++alive_count;
  }
  MoveOnly(const MoveOnly&) = delete;
  MoveOnly& operator=(const MoveOnly&) = delete;
  MoveOnly(MoveOnly&& other) noexcept : value(other.value) {
    ++alive_count;
  }
  MoveOnly& operator=(MoveOnly&&) = default;
  ~MoveOnly() {
    --alive_count;
  }
};

}  // namespace

// =====================================================================
// Часть 1: однопоточные тесты
// =====================================================================

TEST("empty list: begin/end/empty [basic]") {
  append_only_lockfree_list<int> list;
  REQUIRE(list.empty());
  REQUIRE(list.begin() == list.end());
}

TEST("push_back [basic]") {
  append_only_lockfree_list<int> list;

  auto it1 = list.push_back(1);
  REQUIRE(!list.empty());
  REQUIRE(*it1 == 1);

  auto it2 = list.push_back(2);
  auto it3 = list.push_back(3);
  REQUIRE(collect(list) == std::vector<int>{1, 2, 3});

  // итератор из push_back должен указывать ровно на добавленную ноду,
  // а не на что-то другое
  REQUIRE(*it2 == 2);
  REQUIRE(*it3 == 3);
  ++it2;
  REQUIRE(it2 == it3);

  // аргументы должны форвардиться в конструктор T, а не просто копировать готовый T
  append_only_lockfree_list<std::string> strings;
  strings.push_back(3, 'x');  // std::string(3, 'x') == "xxx"
  strings.push_back("hello");
  REQUIRE(collect(strings) == std::vector<std::string>{"xxx", "hello"});

  // тот же контракт (порядок + корректность) должен держаться и на большом количестве вставок
  append_only_lockfree_list<int> big;
  constexpr int n = 2000;
  for (int i = 0; i < n; ++i)
    big.push_back(i);
  std::vector<int> expected(n);
  std::iota(expected.begin(), expected.end(), 0);
  REQUIRE(collect(big) == expected);
}

TEST("push_back_hint [basic]") {
  append_only_lockfree_list<int> list;
  auto it1 = list.push_back(1);
  list.push_back(2);
  list.push_back(3);

  // hint с головы списка всё равно обязан долистать до настоящего хвоста
  // (attach_after_last ищет последнюю ноду начиная от hint, а не вставляет сразу после него)
  list.push_back_hint(it1, 4);
  REQUIRE(collect(list) == std::vector<int>{1, 2, 3, 4});

  // если каждый раз передавать hint = предыдущий результат push_back_hint,
  // пересканирования списка не будет вовсе
  append_only_lockfree_list<int> chained;
  auto tail = chained.push_back(1);
  for (int i = 2; i <= 5; ++i)
    tail = chained.push_back_hint(tail, i);
  REQUIRE(collect(chained) == std::vector<int>{1, 2, 3, 4, 5});
}

TEST("push_back_list [basic]") {
  append_only_lockfree_list<int> a;
  append_only_lockfree_list<int> b;

  // объединение двух пустых списков ничего не меняет
  a.push_back_list(std::move(b));
  REQUIRE(a.empty());

  a.push_back(1);
  a.push_back(2);

  // объединение с пустым источником — no-op
  a.push_back_list(std::move(b));
  REQUIRE(collect(a) == std::vector<int>{1, 2});

  // наполняем b и переносим её ноды в a: ноды физически перевешиваются,
  // порядок сохраняется, а b должна полностью опустеть
  b.push_back(3);
  b.push_back(4);
  a.push_back_list(std::move(b));
  REQUIRE(collect(a) == std::vector<int>{1, 2, 3, 4});
  REQUIRE(b.empty());
  REQUIRE(b.begin() == b.end());
}

TEST("iterator / foreach_node [basic]") {
  append_only_lockfree_list<int> list;
  list.push_back(1);
  list.push_back(2);
  list.push_back(3);

  // постфиксный ++ должен вернуть позицию ДО инкремента
  auto it = list.begin();
  auto prev = it++;
  REQUIRE(*prev == 1);
  REQUIRE(*it == 2);

  // foreach_node обязан посетить все ноды, включая головную
  std::vector<int> visited;
  list.foreach_node([&](auto* node) { visited.push_back(node->value); });
  REQUIRE(visited == std::vector<int>{1, 2, 3});

  // список должен работать с move-only типом...
  append_only_lockfree_list<MoveOnly> movable;
  movable.push_back(1);
  movable.push_back(2);
  std::vector<int> values;
  for (auto mit = movable.begin(); mit != movable.end(); ++mit)
    values.push_back((*mit).value);
  REQUIRE(values == std::vector<int>{1, 2});

  // ...и вызвать деструктор у ВСЕХ нод при своём разрушении (без утечек и double free)
  MoveOnly::alive_count = 0;
  {
    append_only_lockfree_list<MoveOnly> scoped;
    for (int i = 0; i < 10; ++i)
      scoped.push_back(i);
    REQUIRE(MoveOnly::alive_count.load() == 10);
  }
  REQUIRE(MoveOnly::alive_count.load() == 0);
}

TEST("erase_if [basic]") {
  append_only_lockfree_list<int> list;
  REQUIRE(list.empty());
  list.erase_if([](int) noexcept { return true; });
  REQUIRE(list.empty());
  list.erase_if([](int) noexcept { return false; });
  REQUIRE(list.empty());
  for (int i = 0; i < 10; ++i)
    list.push_back(i);
  std::vector<int> expected(10);
  std::iota(expected.begin(), expected.end(), 0);
  REQUIRE(collect(list) == expected);
  list.erase_if([](int) noexcept { return false; });
  REQUIRE(collect(list) == expected);
  auto cond1 = [](const int& x) noexcept { return x > 7; };
  std::erase_if(expected, cond1);
  list.erase_if(cond1);
  REQUIRE(collect(list) == expected);

  auto cond2 = [](const int& x) noexcept { return x < 3; };
  std::erase_if(expected, cond1);
  list.erase_if(cond1);
  REQUIRE(collect(list) == expected);

  auto cond3 = [](const int& x) noexcept { return true; };
  list.erase_if(cond3);
  REQUIRE(list.empty());
}

TEST("const_iterator [basic]") {
  using T = append_only_lockfree_list<int>;
  T list;
  list.push_back(5);
  T::iterator it = list.begin();
  T::const_iterator cit = it;
  T::iterator _ = cit;
  REQUIRE(it == cit);
  REQUIRE(cit == it);
  REQUIRE(it == _);
  auto& cv = *cit;
  auto& v = *it;
  REQUIRE(cv == v);
  static_assert(std::is_same_v<decltype(cv), const int&>);
  using I1 = std::iterator_traits<T::const_iterator>;
  using I2 = std::iterator_traits<std::vector<int>::const_iterator>;
  static_assert(std::is_same_v<I1::pointer, I2::pointer>);
  static_assert(std::is_same_v<I1::reference, I2::reference>);
  static_assert(std::is_same_v<I1::value_type, I2::value_type>);
  static_assert(std::forward_iterator<T::iterator>);
  static_assert(std::forward_iterator<T::const_iterator>);
}

// =====================================================================
// Часть 2: многопоточные тесты
// =====================================================================

TEST("push_back concurrent]") {
  append_only_lockfree_list<int> list;

  constexpr int num_threads = 8;
  constexpr int items_per_thread = 2000;
  std::atomic<bool> go{false};

  std::vector<std::thread> threads;
  threads.reserve(num_threads);
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([&list, &go, t] {
      // барьер синхронизирует старт потоков, чтобы гонки вокруг CAS
      // в attach_after_last были максимально плотными
      while (!go.load(std::memory_order::acquire)) {
      }
      for (int i = 0; i < items_per_thread; ++i)
        list.push_back(t * items_per_thread + i);
    });
  }
  go.store(true, std::memory_order::release);
  for (auto& th : threads)
    th.join();

  auto values = collect(list);
  REQUIRE(values.size() == static_cast<size_t>(num_threads * items_per_thread));

  // ни одно значение не должно потеряться или задублироваться
  std::set<int> unique_values(values.begin(), values.end());
  REQUIRE(unique_values.size() == values.size());
  REQUIRE(*unique_values.begin() == 0);
  REQUIRE(*unique_values.rbegin() == num_threads * items_per_thread - 1);

  // при этом порядок вставок ВНУТРИ каждого потока обязан сохраниться,
  // даже если между потоками элементы перемешаны
  std::vector<int> last_seen_per_thread(num_threads, -1);
  for (int v : values) {
    int thread_id = v / items_per_thread;
    int seq = v % items_per_thread;
    REQUIRE(seq > last_seen_per_thread[thread_id]);
    last_seen_per_thread[thread_id] = seq;
  }
  for (int t = 0; t < num_threads; ++t)
    REQUIRE(last_seen_per_thread[t] == items_per_thread - 1);
}

TEST("push_back_hint [concurrent]") {
  append_only_lockfree_list<int> list;
  auto first = list.push_back(-1);

  constexpr int num_threads = 6;
  constexpr int items_per_thread = 500;

  std::vector<std::thread> threads;
  threads.reserve(num_threads);
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([&list, first, t] {
      auto local_tail = first;
      for (int i = 0; i < items_per_thread; ++i)
        local_tail = list.push_back_hint(local_tail, t * items_per_thread + i);
    });
  }
  for (auto& th : threads)
    th.join();

  auto values = collect(list);
  REQUIRE(values.size() == static_cast<size_t>(1 + num_threads * items_per_thread));

  std::set<int> unique_values(values.begin() + 1, values.end());  // без заглушки
  REQUIRE(unique_values.size() == static_cast<size_t>(num_threads * items_per_thread));
}

TEST("correctly reads [concurrent]") {
  append_only_lockfree_list<int> list;
  list.push_back(0);

  std::atomic<bool> stop{false};
  std::atomic<bool> reader_saw_corruption{false};
  std::atomic<size_t> reader_iterations{0};

  std::thread writer([&] {
    for (int i = 1; i <= 20000; ++i)
      list.push_back(i);
    stop = true;
  });

  std::thread reader([&] {
    while (!stop.load()) {
      int prev = -1;
      for (auto it = list.begin(); it != list.end(); ++it) {
        int v = *it;
        if (v < prev) {
          reader_saw_corruption = true;
          break;
        }
        prev = v;
      }
      ++reader_iterations;
    }
  });

  writer.join();
  reader.join();

  REQUIRE(!reader_saw_corruption.load());
  REQUIRE(collect(list).size() == 20001);
}

TEST("push_back_list [concurrent]") {
  append_only_lockfree_list<int> shared;
  shared.push_back(-1);

  constexpr int num_threads = 6;
  constexpr int items_per_thread = 800;

  std::vector<std::thread> threads;
  threads.reserve(num_threads);
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([&shared, t] {
      // у каждого потока свой локальный список, который целиком перевешивается
      // в общий; конкурентные push_back_list с разными источниками не должны
      // портить общий список друг другу
      append_only_lockfree_list<int> local;
      for (int i = 0; i < items_per_thread; ++i)
        local.push_back(t * items_per_thread + i);
      shared.push_back_list(std::move(local));
    });
  }
  for (auto& th : threads)
    th.join();

  auto values = collect(shared);
  REQUIRE(values.size() == static_cast<size_t>(1 + num_threads * items_per_thread));

  std::set<int> unique_values(values.begin() + 1, values.end());
  REQUIRE(unique_values.size() == static_cast<size_t>(num_threads * items_per_thread));
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
