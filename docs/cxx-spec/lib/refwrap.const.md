# 20.14.5.1 Constructors and destructor [refwrap.const]

[Contents](../README.md) · up: [20.14.5 Class template `reference_wrapper` \[refwrap\]](refwrap.md) · prev: [20.14.5 Class template `reference_wrapper` \[refwrap\]](refwrap.md) · next: [20.14.5.2 Assignment \[refwrap.assign\]](refwrap.assign.md)

```cpp
template<class U>
  constexpr reference_wrapper(U&& u) noexcept(see below);
```

**1** Let `FUN` denote the exposition-only functions

```cpp
void FUN(T&) noexcept;
void FUN(T&&) = delete;
```

**2** *Constraints:* The expression `FUN(declval<U>())` is well-formed and `is_same_v<remove_cvref_t<U>, reference_wrapper>` is `false`.

**3** *Effects:* Creates a variable `r` as if by `T& r = std::forward<U>(u)`, then constructs a `reference_wrapper` object that stores a reference to `r`.

**4** *Remarks:* The expression inside `noexcept` is equivalent to `noexcept(FUN(declval<U>()))`.

```cpp
constexpr reference_wrapper(const reference_wrapper& x) noexcept;
```

**5** *Effects:* Constructs a `reference_wrapper` object that stores a reference to `x.get()`.
