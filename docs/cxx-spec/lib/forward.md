# 20.2.4 Forward/move helpers [forward]

[Contents](../README.md) · up: [20.2 Utility components \[utility\]](utility.md) · prev: [20.2.3 `exchange` \[utility.exchange\]](utility.exchange.md) · next: [20.2.5 Function template `as_const` \[utility.as.const\]](utility.as.const.md)

**1** The library provides templated helper functions to simplify applying move semantics to an lvalue and to simplify the implementation of forwarding functions. All functions specified in this subclause are signal-safe ([[support.signal]](support.signal.md)).

```cpp
template<class T> constexpr T&& forward(remove_reference_t<T>& t) noexcept;
template<class T> constexpr T&& forward(remove_reference_t<T>&& t) noexcept;
```

**2** *Mandates:* For the second overload, `is_lvalue_reference_v<T>` is `false`.

**3** *Returns:* `static_cast<T&&>(t)`.

**4** [*Example 1*:

```cpp
template<class T, class A1, class A2>
shared_ptr<T> factory(A1&& a1, A2&& a2) {
  return shared_ptr<T>(new T(std::forward<A1>(a1), std::forward<A2>(a2)));
}

struct A {
  A(int&, const double&);
};

void g() {
  shared_ptr<A> sp1 = factory<A>(2, 1.414); // error: 2 will not bind to int&
  int i = 2;
  shared_ptr<A> sp2 = factory<A>(i, 1.414); // OK
}
```

In the first call to `factory`, `A1` is deduced as `int`, so 2 is forwarded to `A`’s constructor as an rvalue. In the second call to `factory`, `A1` is deduced as `int&`, so `i` is forwarded to `A`’s constructor as an lvalue. In both cases, `A2` is deduced as `double`, so 1.414 is forwarded to `A`’s constructor as an rvalue. — *end example*]

```cpp
template<class T> constexpr remove_reference_t<T>&& move(T&& t) noexcept;
```

**5** *Returns:* `static_cast<remove_reference_t<T>&&>(t)`.

**6** [*Example 2*:

```cpp
template<class T, class A1>
shared_ptr<T> factory(A1&& a1) {
  return shared_ptr<T>(new T(std::forward<A1>(a1)));
}

struct A {
  A();
  A(const A&);      // copies from lvalues
  A(A&&);           // moves from rvalues
};

void g() {
  A a;
  shared_ptr<A> sp1 = factory<A>(a);                // ``a'' binds to A(const A&)
  shared_ptr<A> sp1 = factory<A>(std::move(a));     // ``a'' binds to A(A&&)
}
```

In the first call to `factory`, `A1` is deduced as `A&`, so `a` is forwarded as a non-const lvalue. This binds to the constructor `A(const A&)`, which copies the value from `a`. In the second call to `factory`, because of the call `std::move(a)`, `A1` is deduced as `A`, so `a` is forwarded as an rvalue. This binds to the constructor `A(A&&)`, which moves the value from `a`. — *end example*]

```cpp
template<class T> constexpr conditional_t<
    !is_nothrow_move_constructible_v<T> && is_copy_constructible_v<T>, const T&, T&&>
  move_if_noexcept(T& x) noexcept;
```

**7** *Returns:* `std::move(x)`.
