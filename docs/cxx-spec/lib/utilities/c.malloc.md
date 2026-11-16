# 20.10.12 C library memory allocation [c.malloc]

[Contents](../../README.md) · up: [20.10 Memory \[memory\]](memory.md) · prev: [20.10.11 `addressof` \[specialized.addressof\]](specialized.addressof.md) · next: [20.11 Smart pointers \[smartptr\]](smartptr.md)

**1** [*Note 1*: The header `<cstdlib>` ([[cstdlib.syn]](../support/cstdlib.syn.md)) declares the functions described in this subclause. — *end note*]

```cpp
void* aligned_alloc(size_t alignment, size_t size);
void* calloc(size_t nmemb, size_t size);
void* malloc(size_t size);
void* realloc(void* ptr, size_t size);
```

**2** *Effects:* These functions have the semantics specified in the C standard library.

**3** *Remarks:* These functions do not attempt to allocate storage by calling `::operator new()` ([[new.delete]](../support/new.delete.md)).

**4** Storage allocated directly with these functions is implicitly declared reachable (see [[basic.stc.dynamic.safety]](../../basic/basic.stc.dynamic.safety.md)) on allocation, ceases to be declared reachable on deallocation, and need not cease to be declared reachable as the result of an `undeclare_reachable()` call. [*Note 2*: This allows existing C libraries to remain unaffected by restrictions on pointers that are not safely derived, at the expense of providing far fewer garbage collection and leak detection options for `malloc()`-allocated objects. It also allows `malloc()` to be implemented with a separate allocation arena, bypassing the normal `declare_reachable()` implementation. The above functions should never intentionally be used as a replacement for `declare_reachable()`, and newly written code is strongly encouraged to treat memory allocated with these functions as though it were allocated with `operator new`. — *end note*]

**5** These functions implicitly create objects ([[intro.object]](../../basic/intro.object.md)) in the returned region of storage and return a pointer to a suitable created object. In the case of `calloc` and `realloc`, the objects are created before the storage is zeroed or copied, respectively.

```cpp
void free(void* ptr);
```

**6** *Effects:* This function has the semantics specified in the C standard library.

**7** *Remarks:* This function does not attempt to deallocate storage by calling `::operator delete()`.

*See also:* ISO C 7.22.3
