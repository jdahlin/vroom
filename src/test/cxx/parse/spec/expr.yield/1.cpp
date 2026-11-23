// [expr.yield] Example 1 (paragraph 1), docs/cxx-spec/expr/expr.yield.md
// needs: P7
// status: annotated

template <typename T>
struct my_generator {
  struct promise_type {
    T current_value;
    /* ... */
    auto yield_value(T v) {
      current_value = std::move(v);
      return std::suspend_always{};
    }
  };
  struct iterator { /* ... */ };
  iterator begin();
  iterator end();
};

my_generator<pair<int,int>> g1() {
  for (int i = i; i < 10; ++i) co_yield {i,i};
}
my_generator<pair<int,int>> g2() {
  for (int i = i; i < 10; ++i) co_yield make_pair(i,i);
}

auto f(int x = co_yield 5);     // error: yield-expression outside of function suspension context // semantic
int a[] = { co_yield 1 };       // error: yield-expression outside of function suspension context // semantic

int main() {
  auto r1 = g1();
  auto r2 = g2();
  assert(std::equal(r1.begin(), r1.end(), r2.begin(), r2.end()));
}
