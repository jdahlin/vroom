# 18.4.12 Concept `default_initializable` [concept.default.init]

[Contents](../README.md) · up: [18.4 Language-related concepts \[concepts.lang\]](concepts.lang.md) · prev: [18.4.11 Concept `constructible_from` \[concept.constructible\]](concept.constructible.md) · next: [18.4.13 Concept `move_constructible` \[concept.moveconstructible\]](concept.moveconstructible.md)

```cpp
template<class T>
  inline constexpr bool is-default-initializable = see below;  // exposition only

template<class T>
  concept default_initializable = constructible_from<T> &&
                                  requires { T{}; } &&
                                  is-default-initializable<T>;
```

**1** For a type `T`, `is-default-initializable<T>` is `true` if and only if the variable definition

```cpp
T t;
```

is well-formed for some invented variable `t`; otherwise it is `false`. Access checking is performed as if in a context unrelated to `T`. Only the validity of the immediate context of the variable initialization is considered.
