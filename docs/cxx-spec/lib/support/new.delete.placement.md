# 17.6.2.3 Non-allocating forms [new.delete.placement]

[Contents](../../README.md) · up: [17.6.2 Storage allocation and deallocation \[new.delete\]](new.delete.md) · prev: [17.6.2.2 Array forms \[new.delete.array\]](new.delete.array.md) · next: [17.6.2.4 Data races \[new.delete.dataraces\]](new.delete.dataraces.md)

**1** These functions are reserved; a C++ program may not define functions that displace the versions in the C++ standard library ([[constraints]](../library/constraints.md)). The provisions of [[basic.stc.dynamic]](../../basic/basic.stc.dynamic.md) do not apply to these reserved placement forms of `operator new` and `operator delete`.

```cpp
[[nodiscard]] void* operator new(std::size_t size, void* ptr) noexcept;
```

**2** *Returns:* `ptr`.

**3** *Remarks:* Intentionally performs no other action.

**4** [*Example 1*: This can be useful for constructing an object at a known address:

```cpp
void* place = operator new(sizeof(Something));
Something* p = new (place) Something();
```

— *end example*]

```cpp
[[nodiscard]] void* operator new[](std::size_t size, void* ptr) noexcept;
```

**5** *Returns:* `ptr`.

**6** *Remarks:* Intentionally performs no other action.

```cpp
void operator delete(void* ptr, void*) noexcept;
```

**7** *Effects:* Intentionally performs no action.

**8** *Preconditions:* If an implementation has strict pointer safety ([[basic.stc.dynamic.safety]](../../basic/basic.stc.dynamic.safety.md)) then `ptr` is a safely-derived pointer.

**9** *Remarks:* Default function called when any part of the initialization in a placement *new-expression* that invokes the library’s non-array placement operator new terminates by throwing an exception ([[expr.new]](../../expr/expr.new.md)).

```cpp
void operator delete[](void* ptr, void*) noexcept;
```

**10** *Effects:* Intentionally performs no action.

**11** *Preconditions:* If an implementation has strict pointer safety ([[basic.stc.dynamic.safety]](../../basic/basic.stc.dynamic.safety.md)) then `ptr` is a safely-derived pointer.

**12** *Remarks:* Default function called when any part of the initialization in a placement *new-expression* that invokes the library’s array placement operator new terminates by throwing an exception ([[expr.new]](../../expr/expr.new.md)).
