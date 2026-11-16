# 7.6.17 Yielding a value [expr.yield]

[Contents](README.md) · up: [7.6 Compound expressions \[expr.compound\]](expr.compound.md) · prev: [7.6.16 Conditional operator \[expr.cond\]](expr.cond.md) · next: [7.6.18 Throwing an exception \[expr.throw\]](expr.throw.md)

```bnf
  yield-expression:
  'co_yield' assignment-expression
  'co_yield' braced-init-list
```

**1** A *yield-expression* shall appear only within a suspension context of a function ([[expr.await]](expr.await.md)). Let *e* be the operand of the *yield-expression* and *p* be an lvalue naming the promise object of the enclosing coroutine ([[dcl.fct.def.coroutine]](dcl.fct.def.coroutine.md)), then the *yield-expression* is equivalent to the expression `co_await`*p*`.yield_value(`*e*`)`.

[*Example 1*:

```cpp
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

auto f(int x = co_yield 5);     // error: yield-expression outside of function suspension context
int a[] = { co_yield 1 };       // error: yield-expression outside of function suspension context

int main() {
  auto r1 = g1();
  auto r2 = g2();
  assert(std::equal(r1.begin(), r1.end(), r2.begin(), r2.end()));
}
```

— *end example*]
