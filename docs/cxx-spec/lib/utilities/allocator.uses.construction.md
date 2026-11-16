# 20.10.8.2 Uses-allocator construction [allocator.uses.construction]

[Contents](../../README.md) · up: [20.10.8 `uses_allocator` \[allocator.uses\]](allocator.uses.md) · prev: [20.10.8.1 `uses_allocator` trait \[allocator.uses.trait\]](allocator.uses.trait.md) · next: [20.10.9 Allocator traits \[allocator.traits\]](allocator.traits.md)

**1** *Uses-allocator construction* with allocator `alloc` and constructor arguments `args...` refers to the construction of an object of type `T` such that `alloc` is passed to the constructor of `T` if `T` uses an allocator type compatible with `alloc`. When applied to the construction of an object of type `T`, it is equivalent to initializing it with the value of the expression `make_obj_using_allocator<T>(alloc, args...)`, described below.

**2** The following utility functions support three conventions for passing `alloc` to a constructor:

- If `T` does not use an allocator compatible with `alloc`, then `alloc` is ignored.
- Otherwise, if `T` has a constructor invocable as `T(allocator_arg, alloc, args...)` (leading-allocator convention), then uses-allocator construction chooses this constructor form.
- Otherwise, if `T` has a constructor invocable as `T(args..., alloc)` (trailing-allocator convention), then uses-allocator construction chooses this constructor form.

**3** The `uses_allocator_construction_args` function template takes an allocator and argument list and produces (as a tuple) a new argument list matching one of the above conventions. Additionally, overloads are provided that treat specializations of `pair` such that uses-allocator construction is applied individually to the `first` and `second` data members. The `make_obj_using_allocator` and `uninitialized_construct_using_allocator` function templates apply the modified constructor arguments to construct an object of type `T` as a return value or in-place, respectively. [*Note 1*: For `uses_allocator_construction_args` and `make_obj_using_allocator`, type `T` is not deduced and must therefore be specified explicitly by the caller. — *end note*]

```cpp
template<class T, class Alloc, class... Args>
  constexpr auto uses_allocator_construction_args(const Alloc& alloc,
                                                  Args&&... args) noexcept -> see below;
```

**4** *Constraints:* `T` is not a specialization of `pair`.

**5** *Returns:* A `tuple` value determined as follows:

- If `uses_allocator_v<T, Alloc>` is `false` and `is_constructible_v<T, Args...>` is `true`, return `forward_as_tuple(std::forward<Args>(args)...)`.

- Otherwise, if `uses_allocator_v<T, Alloc>` is `true` and `is_constructible_v<T, allocator_arg_t, const Alloc&, Args...>` is `true`, return

  ```cpp
  tuple<allocator_arg_t, const Alloc&, Args&&...>(
    allocator_arg, alloc, std::forward<Args>(args)...)
  ```

- Otherwise, if `uses_allocator_v<T, Alloc>` is `true` and `is_constructible_v<T, Args..., const Alloc&>` is `true`, return `forward_as_tuple(std::forward<Args>(args)..., alloc)`.

- Otherwise, the program is ill-formed.

[*Note 2*: This definition prevents a silent failure to pass the allocator to a constructor of a type for which `uses_allocator_v<T, Alloc>` is `true`. — *end note*]

```cpp
template<class T, class Alloc, class Tuple1, class Tuple2>
  constexpr auto uses_allocator_construction_args(const Alloc& alloc, piecewise_construct_t,
                                                  Tuple1&& x, Tuple2&& y)
                                                  noexcept -> see below;
```

**6** *Constraints:* `T` is a specialization of `pair`.

**7** *Effects:* For `T` specified as `pair<T1, T2>`, equivalent to:

```cpp
return make_tuple(
  piecewise_construct,
  apply([&alloc](auto&&... args1) {
          return uses_allocator_construction_args<T1>(
            alloc, std::forward<decltype(args1)>(args1)...);
        }, std::forward<Tuple1>(x)),
  apply([&alloc](auto&&... args2) {
          return uses_allocator_construction_args<T2>(
            alloc, std::forward<decltype(args2)>(args2)...);
        }, std::forward<Tuple2>(y)));
```

```cpp
template<class T, class Alloc>
  constexpr auto uses_allocator_construction_args(const Alloc& alloc) noexcept -> see below;
```

**8** *Constraints:* `T` is a specialization of `pair`.

**9** *Effects:* Equivalent to:

```cpp
return uses_allocator_construction_args<T>(alloc, piecewise_construct,
                                           tuple<>{}, tuple<>{});
```

```cpp
template<class T, class Alloc, class U, class V>
  constexpr auto uses_allocator_construction_args(const Alloc& alloc,
                                                  U&& u, V&& v) noexcept -> see below;
```

**10** *Constraints:* `T` is a specialization of `pair`.

**11** *Effects:* Equivalent to:

```cpp
return uses_allocator_construction_args<T>(alloc, piecewise_construct,
                                           forward_as_tuple(std::forward<U>(u)),
                                           forward_as_tuple(std::forward<V>(v)));
```

```cpp
template<class T, class Alloc, class U, class V>
  constexpr auto uses_allocator_construction_args(const Alloc& alloc,
                                                  const pair<U,V>& pr) noexcept -> see below;
```

**12** *Constraints:* `T` is a specialization of `pair`.

**13** *Effects:* Equivalent to:

```cpp
return uses_allocator_construction_args<T>(alloc, piecewise_construct,
                                           forward_as_tuple(pr.first),
                                           forward_as_tuple(pr.second));
```

```cpp
template<class T, class Alloc, class U, class V>
  constexpr auto uses_allocator_construction_args(const Alloc& alloc,
                                                  pair<U,V>&& pr) noexcept -> see below;
```

**14** *Constraints:* `T` is a specialization of `pair`.

**15** *Effects:* Equivalent to:

```cpp
return uses_allocator_construction_args<T>(alloc, piecewise_construct,
                                           forward_as_tuple(std::move(pr).first),
                                           forward_as_tuple(std::move(pr).second));
```

```cpp
template<class T, class Alloc, class... Args>
  constexpr T make_obj_using_allocator(const Alloc& alloc, Args&&... args);
```

**16** *Effects:* Equivalent to:

```cpp
return make_from_tuple<T>(uses_allocator_construction_args<T>(
                            alloc, std::forward<Args>(args)...));
```

```cpp
template<class T, class Alloc, class... Args>
  constexpr T* uninitialized_construct_using_allocator(T* p, const Alloc& alloc, Args&&... args);
```

**17** *Effects:* Equivalent to:

```cpp
return apply([&]<class... U>(U&&... xs) {
       return construct_at(p, std::forward<U>(xs)...);
     }, uses_allocator_construction_args<T>(alloc, std::forward<Args>(args)...));
```
