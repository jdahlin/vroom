# 6.7.5.4 Dynamic storage duration [basic.stc.dynamic]

[Contents](README.md) · up: [6.7.5 Storage duration \[basic.stc\]](basic.stc.md) · prev: [6.7.5.3 Automatic storage duration \[basic.stc.auto\]](basic.stc.auto.md) · next: [6.7.5.4.1 Allocation functions \[basic.stc.dynamic.allocation\]](basic.stc.dynamic.allocation.md)

**1** Objects can be created dynamically during program execution ([[intro.execution]](intro.execution.md)), using *new-expression*s ([[expr.new]](expr.new.md)), and destroyed using *delete-expression*s ([[expr.delete]](expr.delete.md)). A C++ implementation provides access to, and management of, dynamic storage via the global *allocation functions* `operator new` and `operator new[]` and the global *deallocation functions* `operator delete` and `operator delete[]`. [*Note 1*: The non-allocating forms described in [[new.delete.placement]](lib/new.delete.placement.md) do not perform allocation or deallocation. — *end note*]

**2** The library provides default definitions for the global allocation and deallocation functions. Some global allocation and deallocation functions are replaceable ([[new.delete]](lib/new.delete.md)). A C++ program shall provide at most one definition of a replaceable allocation or deallocation function. Any such function definition replaces the default version provided in the library ([[replacement.functions]](lib/replacement.functions.md)). The following allocation and deallocation functions ([[support.dynamic]](lib/support.dynamic.md)) are implicitly declared in global scope in each translation unit of a program.

```cpp
[[nodiscard]] void* operator new(std::size_t);
[[nodiscard]] void* operator new(std::size_t, std::align_val_t);

void operator delete(void*) noexcept;
void operator delete(void*, std::size_t) noexcept;
void operator delete(void*, std::align_val_t) noexcept;
void operator delete(void*, std::size_t, std::align_val_t) noexcept;

[[nodiscard]] void* operator new[](std::size_t);
[[nodiscard]] void* operator new[](std::size_t, std::align_val_t);

void operator delete[](void*) noexcept;
void operator delete[](void*, std::size_t) noexcept;
void operator delete[](void*, std::align_val_t) noexcept;
void operator delete[](void*, std::size_t, std::align_val_t) noexcept;
```

These implicit declarations introduce only the function names `operator` `new`, `operator` `new[]`, `operator` `delete`, and `operator` `delete[]`. [*Note 2*: The implicit declarations do not introduce the names `std`, `std::size_t`, `std::align_val_t`, or any other names that the library uses to declare these names. Thus, a *new-expression*, *delete-expression*, or function call that refers to one of these functions without importing or including the header `<new>` ([[new.syn]](lib/new.syn.md)) is well-formed. However, referring to `std` or `std::size_t` or `std::align_val_t` is ill-formed unless the name has been declared by importing or including the appropriate header. — *end note*] Allocation and/or deallocation functions may also be declared and defined for any class ([[class.free]](class.free.md)).

**3** If the behavior of an allocation or deallocation function does not satisfy the semantic constraints specified in [[basic.stc.dynamic.allocation]](basic.stc.dynamic.allocation.md) and [[basic.stc.dynamic.deallocation]](basic.stc.dynamic.deallocation.md), the behavior is undefined.

## Subclauses

- [6.7.5.4.1 Allocation functions \[basic.stc.dynamic.allocation\]](basic.stc.dynamic.allocation.md)
- [6.7.5.4.2 Deallocation functions \[basic.stc.dynamic.deallocation\]](basic.stc.dynamic.deallocation.md)
- [6.7.5.4.3 Safely-derived pointers \[basic.stc.dynamic.safety\]](basic.stc.dynamic.safety.md)
