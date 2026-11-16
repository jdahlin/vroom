# 20.8.3.2 Assignment [any.assign]

[Contents](../../README.md) · up: [20.8.3 Class `any` \[any.class\]](any.class.md) · prev: [20.8.3.1 Construction and destruction \[any.cons\]](any.cons.md) · next: [20.8.3.3 Modifiers \[any.modifiers\]](any.modifiers.md)

```cpp
any& operator=(const any& rhs);
```

**1** *Effects:* As if by `any(rhs).swap(*this)`. No effects if an exception is thrown.

**2** *Returns:* `*this`.

**3** *Throws:* Any exceptions arising from the copy constructor for the contained value.

```cpp
any& operator=(any&& rhs) noexcept;
```

**4** *Effects:* As if by `any(std::move(rhs)).swap(*this)`.

**5** *Returns:* `*this`.

**6** *Postconditions:* The state of `*this` is equivalent to the original state of `rhs`.

```cpp
template<class T>
  any& operator=(T&& rhs);
```

**7** Let `VT` be `decay_t<T>`.

**8** *Constraints:* `VT` is not the same type as `any` and `is_copy_constructible_v<VT>` is `true`.

**9** *Preconditions:* `VT` meets the *Cpp17CopyConstructible* requirements.

**10** *Effects:* Constructs an object `tmp` of type `any` that contains an object of type `VT` direct-initialized with `std::forward<T>(rhs)`, and `tmp.swap(*this)`. No effects if an exception is thrown.

**11** *Returns:* `*this`.

**12** *Throws:* Any exception thrown by the selected constructor of `VT`.
