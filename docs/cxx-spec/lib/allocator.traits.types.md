# 20.10.9.1 Member types [allocator.traits.types]

[Contents](../README.md) · up: [20.10.9 Allocator traits \[allocator.traits\]](allocator.traits.md) · prev: [20.10.9 Allocator traits \[allocator.traits\]](allocator.traits.md) · next: [20.10.9.2 Static member functions \[allocator.traits.members\]](allocator.traits.members.md)

```cpp
using pointer = see below;
```

**1** *Type:* `Alloc::pointer` if the *qualified-id* `Alloc::pointer` is valid and denotes a type ([[temp.deduct]](../temp.deduct.md)); otherwise, `value_type*`.

```cpp
using const_pointer = see below;
```

**2** *Type:* `Alloc::const_pointer` if the *qualified-id* `Alloc::const_pointer` is valid and denotes a type ([[temp.deduct]](../temp.deduct.md)); otherwise, `pointer_traits<pointer>::rebind<const value_type>`.

```cpp
using void_pointer = see below;
```

**3** *Type:* `Alloc::void_pointer` if the *qualified-id* `Alloc::void_pointer` is valid and denotes a type ([[temp.deduct]](../temp.deduct.md)); otherwise, `pointer_traits<pointer>::rebind<void>`.

```cpp
using const_void_pointer = see below;
```

**4** *Type:* `Alloc::const_void_pointer` if the *qualified-id* `Alloc::const_void_pointer` is valid and denotes a type ([[temp.deduct]](../temp.deduct.md)); otherwise, `pointer_traits<pointer>::rebind<const void>`.

```cpp
using difference_type = see below;
```

**5** *Type:* `Alloc::difference_type` if the *qualified-id* `Alloc::difference_type` is valid and denotes a type ([[temp.deduct]](../temp.deduct.md)); otherwise, `pointer_traits<pointer>::difference_type`.

```cpp
using size_type = see below;
```

**6** *Type:* `Alloc::size_type` if the *qualified-id* `Alloc::size_type` is valid and denotes a type ([[temp.deduct]](../temp.deduct.md)); otherwise, `make_unsigned_t<difference_type>`.

```cpp
using propagate_on_container_copy_assignment = see below;
```

**7** *Type:* `Alloc::propagate_on_container_copy_assignment` if the *qualified-id* `Alloc::propagate_on_container_copy_assignment` is valid and denotes a type ([[temp.deduct]](../temp.deduct.md)); otherwise `false_type`.

```cpp
using propagate_on_container_move_assignment = see below;
```

**8** *Type:* `Alloc::propagate_on_container_move_assignment` if the *qualified-id* `Alloc::propagate_on_container_move_assignment` is valid and denotes a type ([[temp.deduct]](../temp.deduct.md)); otherwise `false_type`.

```cpp
using propagate_on_container_swap = see below;
```

**9** *Type:* `Alloc::propagate_on_container_swap` if the *qualified-id* `Alloc::propagate_on_container_swap` is valid and denotes a type ([[temp.deduct]](../temp.deduct.md)); otherwise `false_type`.

```cpp
using is_always_equal = see below;
```

**10** *Type:* `Alloc::is_always_equal` if the *qualified-id* `Alloc::is_always_equal` is valid and denotes a type ([[temp.deduct]](../temp.deduct.md)); otherwise `is_empty<Alloc>::type`.

```cpp
template<class T> using rebind_alloc = see below;
```

**11** *Alias template:* `Alloc::rebind<T>::other` if the *qualified-id* `Alloc::rebind<T>::other` is valid and denotes a type ([[temp.deduct]](../temp.deduct.md)); otherwise, `Alloc<T, Args>` if `Alloc` is a class template instantiation of the form `Alloc<U, Args>`, where `Args` is zero or more type arguments; otherwise, the instantiation of `rebind_alloc` is ill-formed.
