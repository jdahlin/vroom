# 20.10.7 Allocator argument tag [allocator.tag]

[Contents](../README.md) · up: [20.10 Memory \[memory\]](memory.md) · prev: [20.10.6 Pointer alignment \[ptr.align\]](ptr.align.md) · next: [20.10.8 `uses_allocator` \[allocator.uses\]](allocator.uses.md)

```cpp
namespace std {
  struct allocator_arg_t { explicit allocator_arg_t() = default; };
  inline constexpr allocator_arg_t allocator_arg{};
}
```

**1** The `allocator_arg_t` struct is an empty class type used as a unique type to disambiguate constructor and function overloading. Specifically, several types (see `tuple` [[tuple]](tuple.md)) have constructors with `allocator_arg_t` as the first argument, immediately followed by an argument of a type that meets the *Cpp17Allocator* requirements ([Table 36](allocator.requirements.md#tab:cpp17.allocator)).
