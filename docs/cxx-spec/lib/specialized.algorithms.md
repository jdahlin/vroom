# 25.11 Specialized `<memory>` algorithms [specialized.algorithms]

[Contents](../README.md) · up: [25 Algorithms library \[algorithms\]](algorithms.md) · prev: [25.10.15 Midpoint \[numeric.ops.midpoint\]](numeric.ops.midpoint.md) · next: [25.11.1 Special memory concepts \[special.mem.concepts\]](special.mem.concepts.md)

**1** The contents specified in this subclause [[specialized.algorithms]](specialized.algorithms.md) are declared in the header `<memory>` ([[memory.syn]](memory.syn.md)).

**2** Unless otherwise specified, if an exception is thrown in the following algorithms, objects constructed by a placement *new-expression* ([[expr.new]](../expr.new.md)) are destroyed in an unspecified order before allowing the exception to propagate.

**3** [*Note 1*: When invoked on ranges of potentially-overlapping subobjects ([[intro.object]](../intro.object.md)), the algorithms specified in this subclause [[specialized.algorithms]](specialized.algorithms.md) result in undefined behavior. — *end note*]

**4** Some algorithms specified in this clause make use of the exposition-only function `voidify`:

```cpp
template<class T>
  constexpr void* voidify(T& obj) noexcept {
    return const_cast<void*>(static_cast<const volatile void*>(addressof(obj)));
  }
```

## Subclauses

- [25.11.1 Special memory concepts \[special.mem.concepts\]](special.mem.concepts.md)
- [25.11.2 `uninitialized_default_construct` \[uninitialized.construct.default\]](uninitialized.construct.default.md)
- [25.11.3 `uninitialized_value_construct` \[uninitialized.construct.value\]](uninitialized.construct.value.md)
- [25.11.4 `uninitialized_copy` \[uninitialized.copy\]](uninitialized.copy.md)
- [25.11.5 `uninitialized_move` \[uninitialized.move\]](uninitialized.move.md)
- [25.11.6 `uninitialized_fill` \[uninitialized.fill\]](uninitialized.fill.md)
- [25.11.7 `construct_at` \[specialized.construct\]](specialized.construct.md)
- [25.11.8 `destroy` \[specialized.destroy\]](specialized.destroy.md)
