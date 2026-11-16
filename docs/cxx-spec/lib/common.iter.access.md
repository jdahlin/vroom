# 23.5.4.4 Accessors [common.iter.access]

[Contents](../README.md) · up: [23.5.4 Common iterators \[iterators.common\]](iterators.common.md) · prev: [23.5.4.3 Constructors and conversions \[common.iter.const\]](common.iter.const.md) · next: [23.5.4.5 Navigation \[common.iter.nav\]](common.iter.nav.md)

```cpp
decltype(auto) operator*();
decltype(auto) operator*() const
  requires dereferenceable<const I>;
```

**1** *Preconditions:* `holds_alternative<I>(v_)`.

**2** *Effects:* Equivalent to: `return *get<I>(v_);`

```cpp
decltype(auto) operator->() const
  requires see below;
```

**3** The expression in the requires clause is equivalent to:

```cpp
indirectly_readable<const I> &&
(requires(const I& i) { i.operator->(); } ||
 is_reference_v<iter_reference_t<I>> ||
 constructible_from<iter_value_t<I>, iter_reference_t<I>>)
```

**4** *Preconditions:* `holds_alternative<I>(v_)`.

**5** *Effects:*

- If `I` is a pointer type or if the expression `get<I>(v_).operator->()` is well-formed, equivalent to: `return get<I>(v_);`

- Otherwise, if `iter_reference_t<I>` is a reference type, equivalent to:

  ```cpp
  auto&& tmp = *get<I>(v_);
  return addressof(tmp);
  ```

- Otherwise, equivalent to: `return proxy(*get<I>(v_));` where `proxy` is the exposition-only class:

  ```cpp
  class proxy {
    iter_value_t<I> keep_;
    proxy(iter_reference_t<I>&& x)
      : keep_(std::move(x)) {}
  public:
    const iter_value_t<I>* operator->() const {
      return addressof(keep_);
    }
  };
  ```
