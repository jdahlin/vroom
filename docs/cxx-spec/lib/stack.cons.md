# 22.6.6.2 Constructors [stack.cons]

[Contents](../README.md) · up: [22.6.6 Class template `stack` \[stack\]](stack.md) · prev: [22.6.6.1 Definition \[stack.defn\]](stack.defn.md) · next: [22.6.6.3 Constructors with allocators \[stack.cons.alloc\]](stack.cons.alloc.md)

```cpp
explicit stack(const Container& cont);
```

**1** *Effects:* Initializes `c` with `cont`.

```cpp
explicit stack(Container&& cont);
```

**2** *Effects:* Initializes `c` with `std::move(cont)`.
