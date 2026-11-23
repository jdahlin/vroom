# 20.10.3 Pointer traits [pointer.traits]

[Contents](../../README.md) · up: [20.10 Memory \[memory\]](memory.md) · prev: [20.10.2 Header `<memory>` synopsis \[memory.syn\]](memory.syn.md) · next: [20.10.3.1 Member types \[pointer.traits.types\]](pointer.traits.types.md)

**1** The class template `pointer_traits` supplies a uniform interface to certain attributes of pointer-like types.

```cpp
namespace std {
  template<class Ptr> struct pointer_traits {
    using pointer         = Ptr;
    using element_type    = see below;
    using difference_type = see below;

    template<class U> using rebind = see below;

    static pointer pointer_to(see below r);
  };

  template<class T> struct pointer_traits<T*> {
    using pointer         = T*;
    using element_type    = T;
    using difference_type = ptrdiff_t;

    template<class U> using rebind = U*;

    static constexpr pointer pointer_to(see below r) noexcept;
  };
}
```

## Subclauses

- [20.10.3.1 Member types \[pointer.traits.types\]](pointer.traits.types.md)
- [20.10.3.2 Member functions \[pointer.traits.functions\]](pointer.traits.functions.md)
- [20.10.3.3 Optional members \[pointer.traits.optmem\]](pointer.traits.optmem.md)
