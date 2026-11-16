# 20.6.3.4 Swap [optional.swap]

[Contents](../README.md) · up: [20.6.3 Class template `optional` \[optional.optional\]](optional.optional.md) · prev: [20.6.3.3 Assignment \[optional.assign\]](optional.assign.md) · next: [20.6.3.5 Observers \[optional.observe\]](optional.observe.md)

```cpp
void swap(optional& rhs) noexcept(see below);
```

**1** *Mandates:* `is_move_constructible_v<T>` is `true`.

**2** *Preconditions:* Lvalues of type `T` are swappable.

**3** *Effects:* See [Table 46](optional.swap.md#tab:optional.swap).

<a id="tab:optional.swap"></a>
**Table 46: `optional::swap(optional&)` effects** [tab:optional.swap]

|  |  | `*this` contains a value | `*this` does not contain a value |
| --- | --- | --- | --- |
| `rhs` contains a value |  | calls `swap(*(*this), *rhs)` | initializes the contained value of `*this` as if direct-non-list-initializing an object of type `T` with the expression `std::move(*rhs)`, followed by `rhs.val->T::~T()`; postcondition is that `*this` contains a value and `rhs` does not contain a value |
| `rhs` does not contain a value |  | initializes the contained value of `rhs` as if direct-non-list-initializing an object of type `T` with the expression `std::move(*(*this))`, followed by `val->T::~T()`; postcondition is that `*this` does not contain a value and `rhs` contains a value | no effect |

**4** *Throws:* Any exceptions thrown by the operations in the relevant part of [Table 46](optional.swap.md#tab:optional.swap).

**5** *Remarks:* The expression inside `noexcept` is equivalent to:

```cpp
is_nothrow_move_constructible_v<T> && is_nothrow_swappable_v<T>
```

If any exception is thrown, the results of the expressions `bool(*this)` and `bool(rhs)` remain unchanged. If an exception is thrown during the call to function `swap`, the state of `*val` and `*rhs.val` is determined by the exception safety guarantee of `swap` for lvalues of `T`. If an exception is thrown during the call to `T`’s move constructor, the state of `*val` and `*rhs.val` is determined by the exception safety guarantee of `T`’s move constructor.
