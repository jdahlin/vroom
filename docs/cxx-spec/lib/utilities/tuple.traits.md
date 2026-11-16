# 20.5.9 Tuple traits [tuple.traits]

[Contents](../../README.md) · up: [20.5 Tuples \[tuple\]](tuple.md) · prev: [20.5.8 Relational operators \[tuple.rel\]](tuple.rel.md) · next: [20.5.10 Tuple specialized algorithms \[tuple.special\]](tuple.special.md)

```cpp
template<class... Types, class Alloc>
  struct uses_allocator<tuple<Types...>, Alloc> : true_type { };
```

**1** *Preconditions:* `Alloc` meets the *Cpp17Allocator* requirements ([Table 36](../library/allocator.requirements.md#tab:cpp17.allocator)).

**2** [*Note 1*: Specialization of this trait informs other library components that `tuple` can be constructed with an allocator, even though it does not have a nested `allocator_type`. — *end note*]
