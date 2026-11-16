# 20.11.1.5 Specialized algorithms [unique.ptr.special]

[Contents](../README.md) · up: [20.11.1 Class template `unique_ptr` \[unique.ptr\]](unique.ptr.md) · prev: [20.11.1.4 Creation \[unique.ptr.create\]](unique.ptr.create.md) · next: [20.11.1.6 I/O \[unique.ptr.io\]](unique.ptr.io.md)

```cpp
template<class T, class D> void swap(unique_ptr<T, D>& x, unique_ptr<T, D>& y) noexcept;
```

**1** *Constraints:* `is_swappable_v<D>` is `true`.

**2** *Effects:* Calls `x.swap(y)`.

```cpp
template<class T1, class D1, class T2, class D2>
  bool operator==(const unique_ptr<T1, D1>& x, const unique_ptr<T2, D2>& y);
```

**3** *Returns:* `x.get() == y.get()`.

```cpp
template<class T1, class D1, class T2, class D2>
  bool operator<(const unique_ptr<T1, D1>& x, const unique_ptr<T2, D2>& y);
```

**4** Let `CT` denote

```cpp
common_type_t<typename unique_ptr<T1, D1>::pointer,
              typename unique_ptr<T2, D2>::pointer>
```

**5** *Mandates:*

- `unique_ptr<T1, D1>::pointer` is implicitly convertible to `CT` and
- `unique_ptr<T2, D2>::pointer` is implicitly convertible to `CT`.

**6** *Preconditions:* The specialization `less<CT>` is a function object type ([[function.objects]](function.objects.md)) that induces a strict weak ordering ([[alg.sorting]](alg.sorting.md)) on the pointer values.

**7** *Returns:* `less<CT>()(x.get(), y.get())`.

```cpp
template<class T1, class D1, class T2, class D2>
  bool operator>(const unique_ptr<T1, D1>& x, const unique_ptr<T2, D2>& y);
```

**8** *Returns:* `y < x`.

```cpp
template<class T1, class D1, class T2, class D2>
  bool operator<=(const unique_ptr<T1, D1>& x, const unique_ptr<T2, D2>& y);
```

**9** *Returns:* `!(y < x)`.

```cpp
template<class T1, class D1, class T2, class D2>
  bool operator>=(const unique_ptr<T1, D1>& x, const unique_ptr<T2, D2>& y);
```

**10** *Returns:* `!(x < y)`.

```cpp
template<class T1, class D1, class T2, class D2>
  requires three_way_comparable_with<typename unique_ptr<T1, D1>::pointer,
                                     typename unique_ptr<T2, D2>::pointer>
  compare_three_way_result_t<typename unique_ptr<T1, D1>::pointer,
                             typename unique_ptr<T2, D2>::pointer>
    operator<=>(const unique_ptr<T1, D1>& x, const unique_ptr<T2, D2>& y);
```

**11** *Returns:* `compare_three_way()(x.get(), y.get())`.

```cpp
template<class T, class D>
  bool operator==(const unique_ptr<T, D>& x, nullptr_t) noexcept;
```

**12** *Returns:* `!x`.

```cpp
template<class T, class D>
  bool operator<(const unique_ptr<T, D>& x, nullptr_t);
template<class T, class D>
  bool operator<(nullptr_t, const unique_ptr<T, D>& x);
```

**13** *Preconditions:* The specialization `less<unique_ptr<T, D>::pointer>` is a function object type ([[function.objects]](function.objects.md)) that induces a strict weak ordering ([[alg.sorting]](alg.sorting.md)) on the pointer values.

**14** *Returns:* The first function template returns

```cpp
less<unique_ptr<T, D>::pointer>()(x.get(), nullptr)
```

The second function template returns

```cpp
less<unique_ptr<T, D>::pointer>()(nullptr, x.get())
```

```cpp
template<class T, class D>
  bool operator>(const unique_ptr<T, D>& x, nullptr_t);
template<class T, class D>
  bool operator>(nullptr_t, const unique_ptr<T, D>& x);
```

**15** *Returns:* The first function template returns `nullptr < x`. The second function template returns `x < nullptr`.

```cpp
template<class T, class D>
  bool operator<=(const unique_ptr<T, D>& x, nullptr_t);
template<class T, class D>
  bool operator<=(nullptr_t, const unique_ptr<T, D>& x);
```

**16** *Returns:* The first function template returns `!(nullptr < x)`. The second function template returns `!(x < nullptr)`.

```cpp
template<class T, class D>
  bool operator>=(const unique_ptr<T, D>& x, nullptr_t);
template<class T, class D>
  bool operator>=(nullptr_t, const unique_ptr<T, D>& x);
```

**17** *Returns:* The first function template returns `!(x < nullptr)`. The second function template returns `!(nullptr < x)`.

```cpp
template<class T, class D>
  requires three_way_comparable_with<typename unique_ptr<T, D>::pointer, nullptr_t>
  compare_three_way_result_t<typename unique_ptr<T, D>::pointer, nullptr_t>
    operator<=>(const unique_ptr<T, D>& x, nullptr_t);
```

**18** *Returns:* `compare_three_way()(x.get(), nullptr)`.
