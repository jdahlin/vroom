# 23.3.2.1 Incrementable traits [incrementable.traits]

[Contents](../../README.md) · up: [23.3.2 Associated types \[iterator.assoc.types\]](iterator.assoc.types.md) · prev: [23.3.2 Associated types \[iterator.assoc.types\]](iterator.assoc.types.md) · next: [23.3.2.2 Indirectly readable traits \[readable.traits\]](readable.traits.md)

**1** To implement algorithms only in terms of incrementable types, it is often necessary to determine the difference type that corresponds to a particular incrementable type. Accordingly, it is required that if `WI` is the name of a type that models the `weakly_incrementable` concept ([[iterator.concept.winc]](iterator.concept.winc.md)), the type

```cpp
iter_difference_t<WI>
```

be defined as the incrementable type’s difference type.

```cpp
namespace std {
  template<class> struct incrementable_traits { };

  template<class T>
    requires is_object_v<T>
  struct incrementable_traits<T*> {
    using difference_type = ptrdiff_t;
  };

  template<class I>
  struct incrementable_traits<const I>
    : incrementable_traits<I> { };

  template<class T>
    requires requires { typename T::difference_type; }
  struct incrementable_traits<T> {
    using difference_type = typename T::difference_type;
  };

  template<class T>
    requires (!requires { typename T::difference_type; } &&
              requires(const T& a, const T& b) { { a - b } -> integral; })
  struct incrementable_traits<T> {
    using difference_type = make_signed_t<decltype(declval<T>() - declval<T>())>;
  };

  template<class T>
    using iter_difference_t = see below;
}
```

**2** Let *R*<sub></sub>*I* be `remove_cvref_t<I>`. The type `iter_difference_t<I>` denotes

- `incrementable_traits<R_()I>::difference_type` if `iterator_traits<R_()I>` names a specialization generated from the primary template, and
- `iterator_traits<R_()I>::difference_type` otherwise.

**3** Users may specialize `incrementable_traits` on program-defined types.
