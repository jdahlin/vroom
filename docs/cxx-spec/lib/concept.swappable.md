# 18.4.9 Concept `swappable` [concept.swappable]

[Contents](../README.md) · up: [18.4 Language-related concepts \[concepts.lang\]](concepts.lang.md) · prev: [18.4.8 Concept `assignable_from` \[concept.assignable\]](concept.assignable.md) · next: [18.4.10 Concept `destructible` \[concept.destructible\]](concept.destructible.md)

**1** Let `t1` and `t2` be equality-preserving expressions that denote distinct equal objects of type `T`, and let `u1` and `u2` similarly denote distinct equal objects of type `U`. [*Note 1*: `t1` and `u1` can denote distinct objects, or the same object. — *end note*] An operation *exchanges the values* denoted by `t1` and `u1` if and only if the operation modifies neither `t2` nor `u2` and:

- If `T` and `U` are the same type, the result of the operation is that `t1` equals `u2` and `u1` equals `t2`.
- If `T` and `U` are different types and `common_reference_with<decltype((t1)), decltype((u1))>` is modeled, the result of the operation is that `C(t1)` equals `C(u2)` and `C(u1)` equals `C(t2)` where `C` is `common_reference_t<decltype((t1)), decltype((u1))>`.

**2** The name `ranges::swap` denotes a customization point object ([[customization.point.object]](customization.point.object.md)). The expression `ranges::swap(E1, E2)` for subexpressions `E1` and `E2` is expression-equivalent to an expression `S` determined as follows:

- `S` is `(void)swap(E1, E2)`[^1] if `E1` or `E2` has class or enumeration type ([[basic.compound]](../basic.compound.md)) and that expression is valid, with overload resolution performed in a context that includes the declaration

  ```cpp
  template<class T>
    void swap(T&, T&) = delete;
  ```

  and does not include a declaration of `ranges::swap`. If the function selected by overload resolution does not exchange the values denoted by `E1` and `E2`, the program is ill-formed, no diagnostic required.

- Otherwise, if `E1` and `E2` are lvalues of array types ([[basic.compound]](../basic.compound.md)) with equal extent and `ranges::swap(*E1, *E2)` is a valid expression, `S` is `(void)ranges::swap_ranges(E1, E2)`, except that `noexcept(S)` is equal to `noexcept(ranges::swap(*E1, *E2))`.

- Otherwise, if `E1` and `E2` are lvalues of the same type `T` that models `move_constructible<T>` and `assignable_from<T&, T>`, `S` is an expression that exchanges the denoted values. `S` is a constant expression if

  - `T` is a literal type ([[basic.types]](../basic.types.md)),

  - both `E1 = std::move(E2)` and `E2 = std::move(E1)` are constant subexpressions ([[defns.const.subexpr]](definitions.md#defns.const.subexpr)), and

  - the full-expressions of the initializers in the declarations

    ```cpp
    T t1(std::move(E1));
    T t2(std::move(E2));
    ```

    are constant subexpressions.

  `noexcept(S)` is equal to `is_nothrow_move_constructible_v<T> && is_nothrow_move_assignable_v<T>`.

- Otherwise, `ranges::swap(E1, E2)` is ill-formed. [*Note 2*: This case can result in substitution failure when `ranges::swap(E1, E2)` appears in the immediate context of a template instantiation. — *end note*]

**3** [*Note 3*: Whenever `ranges::swap(E1, E2)` is a valid expression, it exchanges the values denoted by `E1` and `E2` and has type `void`. — *end note*]

```cpp
template<class T>
  concept swappable = requires(T& a, T& b) { ranges::swap(a, b); };
```

```cpp
template<class T, class U>
  concept swappable_with =
    common_reference_with<T, U> &&
    requires(T&& t, U&& u) {
      ranges::swap(std::forward<T>(t), std::forward<T>(t));
      ranges::swap(std::forward<U>(u), std::forward<U>(u));
      ranges::swap(std::forward<T>(t), std::forward<U>(u));
      ranges::swap(std::forward<U>(u), std::forward<T>(t));
    };
```

**4** [*Note 4*: The semantics of the `swappable` and `swappable_with` concepts are fully defined by the `ranges::swap` customization point. — *end note*]

**5** [*Example 1*: User code can ensure that the evaluation of `swap` calls is performed in an appropriate context under the various conditions as follows:

```cpp
#include <cassert>
#include <concepts>
#include <utility>

namespace ranges = std::ranges;

template<class T, std::swappable_with<T> U>
void value_swap(T&& t, U&& u) {
  ranges::swap(std::forward<T>(t), std::forward<U>(u));
}

template<std::swappable T>
void lv_swap(T& t1, T& t2) {
  ranges::swap(t1, t2);
}

namespace N {
  struct A { int m; };
  struct Proxy {
    A* a;
    Proxy(A& a) : a{&a} {}
    friend void swap(Proxy x, Proxy y) {
      ranges::swap(*x.a, *y.a);
    }
  };
  Proxy proxy(A& a) { return Proxy{ a }; }
}

int main() {
  int i = 1, j = 2;
  lv_swap(i, j);
  assert(i == 2 && j == 1);

  N::A a1 = { 5 }, a2 = { -5 };
  value_swap(a1, proxy(a2));
  assert(a1.m == -5 && a2.m == 5);
}
```

— *end example*]

[^1]: The name `swap` is used here unqualified.
