# 20.8.3.1 Construction and destruction [any.cons]

[Contents](../../README.md) · up: [20.8.3 Class `any` \[any.class\]](any.class.md) · prev: [20.8.3 Class `any` \[any.class\]](any.class.md) · next: [20.8.3.2 Assignment \[any.assign\]](any.assign.md)

```cpp
constexpr any() noexcept;
```

**1** *Postconditions:* `has_value()` is `false`.

```cpp
any(const any& other);
```

**2** *Effects:* If `other.has_value()` is `false`, constructs an object that has no value. Otherwise, equivalent to `any(in_place_type<T>, any_cast<const T&>(other))` where `T` is the type of the contained value.

**3** *Throws:* Any exceptions arising from calling the selected constructor for the contained value.

```cpp
any(any&& other) noexcept;
```

**4** *Effects:* If `other.has_value()` is `false`, constructs an object that has no value. Otherwise, constructs an object of type `any` that contains either the contained value of `other`, or contains an object of the same type constructed from the contained value of `other` considering that contained value as an rvalue.

```cpp
template<class T>
  any(T&& value);
```

**5** Let `VT` be `decay_t<T>`.

**6** *Constraints:* `VT` is not the same type as `any`, `VT` is not a specialization of `in_place_type_t`, and `is_copy_constructible_v<VT>` is `true`.

**7** *Preconditions:* `VT` meets the *Cpp17CopyConstructible* requirements.

**8** *Effects:* Constructs an object of type `any` that contains an object of type `VT` direct-initialized with `std::forward<T>(value)`.

**9** *Throws:* Any exception thrown by the selected constructor of `VT`.

```cpp
template<class T, class... Args>
  explicit any(in_place_type_t<T>, Args&&... args);
```

**10** Let `VT` be `decay_t<T>`.

**11** *Constraints:* `is_copy_constructible_v<VT>` is `true` and `is_constructible_v<VT, Args...>` is `true`.

**12** *Preconditions:* `VT` meets the *Cpp17CopyConstructible* requirements.

**13** *Effects:* Initializes the contained value as if direct-non-list-initializing an object of type `VT` with the arguments `std::forward<Args>(args)...`.

**14** *Postconditions:* `*this` contains a value of type `VT`.

**15** *Throws:* Any exception thrown by the selected constructor of `VT`.

```cpp
template<class T, class U, class... Args>
  explicit any(in_place_type_t<T>, initializer_list<U> il, Args&&... args);
```

**16** Let `VT` be `decay_t<T>`.

**17** *Constraints:* `is_copy_constructible_v<VT>` is `true` and `is_constructible_v<VT, initializer_list<U>&, Args...>` is `true`.

**18** *Preconditions:* `VT` meets the *Cpp17CopyConstructible* requirements.

**19** *Effects:* Initializes the contained value as if direct-non-list-initializing an object of type `VT` with the arguments `il, std::forward<Args>(args)...`.

**20** *Postconditions:* `*this` contains a value.

**21** *Throws:* Any exception thrown by the selected constructor of `VT`.

```cpp
~any();
```

**22** *Effects:* As if by `reset()`.
