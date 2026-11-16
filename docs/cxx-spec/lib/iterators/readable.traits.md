# 23.3.2.2 Indirectly readable traits [readable.traits]

[Contents](../../README.md) · up: [23.3.2 Associated types \[iterator.assoc.types\]](iterator.assoc.types.md) · prev: [23.3.2.1 Incrementable traits \[incrementable.traits\]](incrementable.traits.md) · next: [23.3.2.3 Iterator traits \[iterator.traits\]](iterator.traits.md)

**1** To implement algorithms only in terms of indirectly readable types, it is often necessary to determine the value type that corresponds to a particular indirectly readable type. Accordingly, it is required that if `R` is the name of a type that models the `indirectly_readable` concept ([[iterator.concept.readable]](iterator.concept.readable.md)), the type

```cpp
iter_value_t<R>
```

be defined as the indirectly readable type’s value type.

```cpp
template<class> struct cond-value-type { };     // exposition only
template<class T>
  requires is_object_v<T>
struct cond-value-type<T> {
  using value_type = remove_cv_t<T>;
};

template<class> struct indirectly_readable_traits { };

template<class T>
struct indirectly_readable_traits<T*>
  : cond-value-type<T> { };

template<class I>
  requires is_array_v<I>
struct indirectly_readable_traits<I> {
  using value_type = remove_cv_t<remove_extent_t<I>>;
};

template<class I>
struct indirectly_readable_traits<const I>
  : indirectly_readable_traits<I> { };

template<class T>
  requires requires { typename T::value_type; }
struct indirectly_readable_traits<T>
  : cond-value-type<typename T::value_type> { };

template<class T>
  requires requires { typename T::element_type; }
struct indirectly_readable_traits<T>
  : cond-value-type<typename T::element_type> { };

template<class T> using iter_value_t = see below;
```

**2** Let *R*<sub></sub>*I* be `remove_cvref_t<I>`. The type `iter_value_t<I>` denotes

- `indirectly_readable_traits<R_()I>::value_type` if `iterator_traits<R_()I>` names a specialization generated from the primary template, and
- `iterator_traits<R_()I>::value_type` otherwise.

**3** Class template `indirectly_readable_traits` may be specialized on program-defined types.

**4** [*Note 1*: Some legacy output iterators define a nested type named `value_type` that is an alias for `void`. These types are not `indirectly_readable` and have no associated value types. — *end note*]

**5** [*Note 2*: Smart pointers like `shared_ptr<int>` are `indirectly_readable` and have an associated value type, but a smart pointer like `shared_ptr<void>` is not `indirectly_readable` and has no associated value type. — *end note*]
