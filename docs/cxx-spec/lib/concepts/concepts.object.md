# 18.6 Object concepts [concepts.object]

[Contents](../../README.md) · up: [18 Concepts library \[concepts\]](concepts.md) · prev: [18.5.4 Concept `totally_ordered` \[concept.totallyordered\]](concept.totallyordered.md) · next: [18.7 Callable concepts \[concepts.callable\]](concepts.callable.md)

**1** This subclause describes concepts that specify the basis of the value-oriented programming style on which the library is based.

```cpp
template<class T>
  concept movable = is_object_v<T> && move_constructible<T> &&
                    assignable_from<T&, T> && swappable<T>;
template<class T>
  concept copyable = copy_constructible<T> && movable<T> && assignable_from<T&, T&> &&
                     assignable_from<T&, const T&> && assignable_from<T&, const T>;
template<class T>
  concept semiregular = copyable<T> && default_initializable<T>;
template<class T>
  concept regular = semiregular<T> && equality_comparable<T>;
```

**2** [*Note 1*: The `semiregular` concept is modeled by types that behave similarly to built-in types like `int`, except that they might not be comparable with `==`. — *end note*]

**3** [*Note 2*: The `regular` concept is modeled by types that behave similarly to built-in types like `int` and that are comparable with `==`. — *end note*]
