# 20.13.2 Member types [allocator.adaptor.types]

[Contents](../../README.md) · up: [20.13 Class template `scoped_allocator_adaptor` \[allocator.adaptor\]](allocator.adaptor.md) · prev: [20.13.1 Header `<scoped_allocator>` synopsis \[allocator.adaptor.syn\]](allocator.adaptor.syn.md) · next: [20.13.3 Constructors \[allocator.adaptor.cnstr\]](allocator.adaptor.cnstr.md)

```cpp
using inner_allocator_type = see below;
```

**1** *Type:* `scoped_allocator_adaptor<OuterAlloc>` if `sizeof...(InnerAllocs)` is zero; otherwise,<br> `scoped_allocator_adaptor<InnerAllocs...>`.

```cpp
using propagate_on_container_copy_assignment = see below;
```

**2** *Type:* `true_type` if `allocator_traits<A>::propagate_on_container_copy_assignment::value` is `true` for any `A` in the set of `OuterAlloc` and `InnerAllocs...`; otherwise, `false_type`.

```cpp
using propagate_on_container_move_assignment = see below;
```

**3** *Type:* `true_type` if `allocator_traits<A>::propagate_on_container_move_assignment::value` is `true` for any `A` in the set of `OuterAlloc` and `InnerAllocs...`; otherwise, `false_type`.

```cpp
using propagate_on_container_swap = see below;
```

**4** *Type:* `true_type` if `allocator_traits<A>::propagate_on_container_swap::value` is `true` for any `A` in the set of `OuterAlloc` and `InnerAllocs...`; otherwise, `false_type`.

```cpp
using is_always_equal = see below;
```

**5** *Type:* `true_type` if `allocator_traits<A>::is_always_equal::value` is `true` for every `A` in the set of `OuterAlloc` and `InnerAllocs...`; otherwise, `false_type`.
