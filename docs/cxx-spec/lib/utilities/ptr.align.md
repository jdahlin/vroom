# 20.10.6 Pointer alignment [ptr.align]

[Contents](../../README.md) · up: [20.10 Memory \[memory\]](memory.md) · prev: [20.10.5 Pointer safety \[util.dynamic.safety\]](util.dynamic.safety.md) · next: [20.10.7 Allocator argument tag \[allocator.tag\]](allocator.tag.md)

```cpp
void* align(size_t alignment, size_t size, void*& ptr, size_t& space);
```

**1** *Preconditions:*

- `alignment` is a power of two
- `ptr` represents the address of contiguous storage of at least `space` bytes

**2** *Effects:* If it is possible to fit `size` bytes of storage aligned by `alignment` into the buffer pointed to by `ptr` with length `space`, the function updates `ptr` to represent the first possible address of such storage and decreases `space` by the number of bytes used for alignment. Otherwise, the function does nothing.

**3** *Returns:* A null pointer if the requested aligned buffer would not fit into the available space, otherwise the adjusted value of `ptr`.

**4** [*Note 1*: The function updates its `ptr` and `space` arguments so that it can be called repeatedly with possibly different `alignment` and `size` arguments for the same buffer. — *end note*]

```cpp
template<size_t N, class T>
  [[nodiscard]] constexpr T* assume_aligned(T* ptr);
```

**5** *Mandates:* `N` is a power of two.

**6** *Preconditions:* `ptr` points to an object `X` of a type similar ([[conv.qual]](../../expr/conv.qual.md)) to `T`, where `X` has alignment `N` ([[basic.align]](../../basic/basic.align.md)).

**7** *Returns:* `ptr`.

**8** *Throws:* Nothing.

**9** [*Note 2*: The alignment assumption on an object `X` expressed by a call to `assume_aligned` may result in generation of more efficient code. It is up to the program to ensure that the assumption actually holds. The call does not cause the compiler to verify or enforce this. An implementation might only make the assumption for those operations on `X` that access `X` through the pointer returned by `assume_aligned`. — *end note*]
