# 22.6.4.2 Constructors [queue.cons]

[Contents](../../README.md) · up: [22.6.4 Class template `queue` \[queue\]](queue.md) · prev: [22.6.4.1 Definition \[queue.defn\]](queue.defn.md) · next: [22.6.4.3 Constructors with allocators \[queue.cons.alloc\]](queue.cons.alloc.md)

```cpp
explicit queue(const Container& cont);
```

**1** *Effects:* Initializes `c` with `cont`.

```cpp
explicit queue(Container&& cont);
```

**2** *Effects:* Initializes `c` with `std::move(cont)`.
