# 24.4.4 Views [range.view]

[Contents](../../README.md) · up: [24.4 Range requirements \[range.req\]](range.req.md) · prev: [24.4.3 Sized ranges \[range.sized\]](range.sized.md) · next: [24.4.5 Other range refinements \[range.refinements\]](range.refinements.md)

**1** The `view` concept specifies the requirements of a `range` type that has constant time move construction, move assignment, and destruction; that is, the cost of these operations is independent of the number of elements in the `view`.

```cpp
template<class T>
  concept view =
    range<T> && movable<T> && default_initializable<T> && enable_view<T>;
```

**2** `T` models `view` only if:

- `T` has 𝑂(1) move construction; and
- `T` has 𝑂(1) move assignment; and
- `T` has 𝑂(1) destruction; and
- `copy_constructible<T>` is `false`, or `T` has 𝑂(1) copy construction; and
- `copyable<T>` is `false`, or `T` has 𝑂(1) copy assignment.

**3** [*Example 1*: Examples of `view`s are:

- A `range` type that wraps a pair of iterators.
- A `range` type that holds its elements by `shared_ptr` and shares ownership with all its copies.
- A `range` type that generates its elements on demand.

Most containers ([[containers]](../containers/containers.md)) are not views since destruction of the container destroys the elements, which cannot be done in constant time. — *end example*]

**4** Since the difference between `range` and `view` is largely semantic, the two are differentiated with the help of `enable_view`.

```cpp
template<class T>
  inline constexpr bool enable_view = derived_from<T, view_base>;
```

**5** *Remarks:* Pursuant to [[namespace.std]](../library/namespace.std.md), users may specialize `enable_view` to `true` for cv-unqualified program-defined types which model `view`, and `false` for types which do not. Such specializations shall be usable in constant expressions ([[expr.const]](../../expr/expr.const.md)) and have type `const bool`.
