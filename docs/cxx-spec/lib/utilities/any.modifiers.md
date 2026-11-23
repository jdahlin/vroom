# 20.8.3.3 Modifiers [any.modifiers]

[Contents](../../README.md) · up: [20.8.3 Class `any` \[any.class\]](any.class.md) · prev: [20.8.3.2 Assignment \[any.assign\]](any.assign.md) · next: [20.8.3.4 Observers \[any.observers\]](any.observers.md)

```cpp
template<class T, class... Args>
  decay_t<T>& emplace(Args&&... args);
```

**1** Let `VT` be `decay_t<T>`.

**2** *Constraints:* `is_copy_constructible_v<VT>` is `true` and `is_constructible_v<VT, Args...>` is `true`.

**3** *Preconditions:* `VT` meets the *Cpp17CopyConstructible* requirements.

**4** *Effects:* Calls `reset()`. Then initializes the contained value as if direct-non-list-initializing an object of type `VT` with the arguments `std::forward<Args>(args)...`.

**5** *Postconditions:* `*this` contains a value.

**6** *Returns:* A reference to the new contained value.

**7** *Throws:* Any exception thrown by the selected constructor of `VT`.

**8** *Remarks:* If an exception is thrown during the call to `VT`’s constructor, `*this` does not contain a value, and any previously contained value has been destroyed.

```cpp
template<class T, class U, class... Args>
  decay_t<T>& emplace(initializer_list<U> il, Args&&... args);
```

**9** Let `VT` be `decay_t<T>`.

**10** *Constraints:* `is_copy_constructible_v<VT>` is `true` and `is_constructible_v<VT, initializer_list<U>&, Args...>` is `true`.

**11** *Preconditions:* `VT` meets the *Cpp17CopyConstructible* requirements.

**12** *Effects:* Calls `reset()`. Then initializes the contained value as if direct-non-list-initializing an object of type `VT` with the arguments `il, std::forward<Args>(args)...`.

**13** *Postconditions:* `*this` contains a value.

**14** *Returns:* A reference to the new contained value.

**15** *Throws:* Any exception thrown by the selected constructor of `VT`.

**16** *Remarks:* If an exception is thrown during the call to `VT`’s constructor, `*this` does not contain a value, and any previously contained value has been destroyed.

```cpp
void reset() noexcept;
```

**17** *Effects:* If `has_value()` is `true`, destroys the contained value.

**18** *Postconditions:* `has_value()` is `false`.

```cpp
void swap(any& rhs) noexcept;
```

**19** *Effects:* Exchanges the states of `*this` and `rhs`.
