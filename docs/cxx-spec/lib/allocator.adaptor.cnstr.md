# 20.13.3 Constructors [allocator.adaptor.cnstr]

[Contents](../README.md) · up: [20.13 Class template `scoped_allocator_adaptor` \[allocator.adaptor\]](allocator.adaptor.md) · prev: [20.13.2 Member types \[allocator.adaptor.types\]](allocator.adaptor.types.md) · next: [20.13.4 Members \[allocator.adaptor.members\]](allocator.adaptor.members.md)

```cpp
scoped_allocator_adaptor();
```

**1** *Effects:* Value-initializes the `OuterAlloc` base class and the `inner` allocator object.

```cpp
template<class OuterA2>
  scoped_allocator_adaptor(OuterA2&& outerAlloc, const InnerAllocs&... innerAllocs) noexcept;
```

**2** *Constraints:* `is_constructible_v<OuterAlloc, OuterA2>` is `true`.

**3** *Effects:* Initializes the `OuterAlloc` base class with `std::forward<OuterA2>(outerAlloc)` and `inner` with `innerAllocs...` (hence recursively initializing each allocator within the adaptor with the corresponding allocator from the argument list).

```cpp
scoped_allocator_adaptor(const scoped_allocator_adaptor& other) noexcept;
```

**4** *Effects:* Initializes each allocator within the adaptor with the corresponding allocator from `other`.

```cpp
scoped_allocator_adaptor(scoped_allocator_adaptor&& other) noexcept;
```

**5** *Effects:* Move constructs each allocator within the adaptor with the corresponding allocator from `other`.

```cpp
template<class OuterA2>
  scoped_allocator_adaptor(
    const scoped_allocator_adaptor<OuterA2, InnerAllocs...>& other) noexcept;
```

**6** *Constraints:* `is_constructible_v<OuterAlloc, const OuterA2&>` is `true`.

**7** *Effects:* Initializes each allocator within the adaptor with the corresponding allocator from `other`.

```cpp
template<class OuterA2>
  scoped_allocator_adaptor(scoped_allocator_adaptor<OuterA2, InnerAllocs...>&& other) noexcept;
```

**8** *Constraints:* `is_constructible_v<OuterAlloc, OuterA2>` is `true`.

**9** *Effects:* Initializes each allocator within the adaptor with the corresponding allocator rvalue from `other`.
