# 24.7.2 Semiregular wrapper [range.semi.wrap]

[Contents](../../README.md) · up: [24.7 Range adaptors \[range.adaptors\]](range.adaptors.md) · prev: [24.7.1 Range adaptor objects \[range.adaptor.object\]](range.adaptor.object.md) · next: [24.7.3 All view \[range.all\]](range.all.md)

**1** Many types in this subclause are specified in terms of an exposition-only class template `semiregular-box`. `semiregular-box<T>` behaves exactly like `optional<T>` with the following differences:

- `semiregular-box<T>` constrains its type parameter `T` with `copy_constructible<T> && is_object_v<T>`.

- If `T` models `default_initializable`, the default constructor of `semiregular-box<T>` is equivalent to:

  ```cpp
  constexpr semiregular-box() noexcept(is_nothrow_default_constructible_v<T>)
    : semiregular-box{in_place}
  { }
  ```

- If `assignable_from<T&, const T&>` is not modeled, the copy assignment operator is equivalent to:

  ```cpp
  semiregular-box& operator=(const semiregular-box& that)
    noexcept(is_nothrow_copy_constructible_v<T>)
  {
    if (that) emplace(*that);
    else reset();
    return *this;
  }
  ```

- If `assignable_from<T&, T>` is not modeled, the move assignment operator is equivalent to:

  ```cpp
  semiregular-box& operator=(semiregular-box&& that)
    noexcept(is_nothrow_move_constructible_v<T>)
  {
    if (that) emplace(std::move(*that));
    else reset();
    return *this;
  }
  ```
