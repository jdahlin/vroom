# 16.5.4.6 Replacement functions [replacement.functions]

[Contents](../../README.md) · up: [16.5.4 Constraints on programs \[constraints\]](constraints.md) · prev: [16.5.4.5 Derived classes \[derived.classes\]](derived.classes.md) · next: [16.5.4.7 Handler functions \[handler.functions\]](handler.functions.md)

**1** [[support]](../support/support.md) through [[thread]](../thread/thread.md) and [[depr]](../../depr/depr.md) describe the behavior of numerous functions defined by the C++ standard library. Under some circumstances, however, certain of these function descriptions also apply to replacement functions defined in the program ([[definitions]](definitions.md)).

**2** A C++ program may provide the definition for any of the following dynamic memory allocation function signatures declared in header `<new>` ([[basic.stc.dynamic]](../../basic/basic.stc.dynamic.md), [[new.syn]](../support/new.syn.md)):

```cpp
operator new(std::size_t)
operator new(std::size_t, std::align_val_t)
operator new(std::size_t, const std::nothrow_t&)
operator new(std::size_t, std::align_val_t, const std::nothrow_t&)
```

```cpp
operator delete(void*)
operator delete(void*, std::size_t)
operator delete(void*, std::align_val_t)
operator delete(void*, std::size_t, std::align_val_t)
operator delete(void*, const std::nothrow_t&)
operator delete(void*, std::align_val_t, const std::nothrow_t&)
```

```cpp
operator new[](std::size_t)
operator new[](std::size_t, std::align_val_t)
operator new[](std::size_t, const std::nothrow_t&)
operator new[](std::size_t, std::align_val_t, const std::nothrow_t&)
```

```cpp
operator delete[](void*)
operator delete[](void*, std::size_t)
operator delete[](void*, std::align_val_t)
operator delete[](void*, std::size_t, std::align_val_t)
operator delete[](void*, const std::nothrow_t&)
operator delete[](void*, std::align_val_t, const std::nothrow_t&)
```

**3** The program’s definitions are used instead of the default versions supplied by the implementation ([[new.delete]](../support/new.delete.md)). Such replacement occurs prior to program startup ([[basic.def.odr]](../../basic/basic.def.odr.md), [[basic.start]](../../basic/basic.start.md)). The program’s declarations shall not be specified as `inline`. No diagnostic is required.
