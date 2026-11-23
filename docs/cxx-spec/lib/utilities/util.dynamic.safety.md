# 20.10.5 Pointer safety [util.dynamic.safety]

[Contents](../../README.md) · up: [20.10 Memory \[memory\]](memory.md) · prev: [20.10.4 Pointer conversion \[pointer.conversion\]](pointer.conversion.md) · next: [20.10.6 Pointer alignment \[ptr.align\]](ptr.align.md)

**1** A complete object is *declared reachable* while the number of calls to `declare_reachable` with an argument referencing the object exceeds the number of calls to `undeclare_reachable` with an argument referencing the object.

```cpp
void declare_reachable(void* p);
```

**2** *Preconditions:* `p` is a safely-derived pointer ([[basic.stc.dynamic.safety]](../../basic/basic.stc.dynamic.safety.md)) or a null pointer value.

**3** *Effects:* If `p` is not null, the complete object referenced by `p` is subsequently declared reachable ([[basic.stc.dynamic.safety]](../../basic/basic.stc.dynamic.safety.md)).

**4** *Throws:* May throw `bad_alloc` if the system cannot allocate additional memory that may be required to track objects declared reachable.

```cpp
template<class T> T* undeclare_reachable(T* p);
```

**5** *Preconditions:* If `p` is not null, the complete object referenced by `p` has been previously declared reachable, and is live ([[basic.life]](../../basic/basic.life.md)) from the time of the call until the last `undeclare_reachable(p)` call on the object.

**6** *Returns:* A safely derived copy of `p` which compares equal to `p`.

**7** *Throws:* Nothing.

**8** [*Note 1*: It is expected that calls to `declare_reachable(p)` will consume a small amount of memory in addition to that occupied by the referenced object until the matching call to `undeclare_reachable(p)` is encountered. Long running programs should arrange that calls are matched. — *end note*]

```cpp
void declare_no_pointers(char* p, size_t n);
```

**9** *Preconditions:* No bytes in the specified range are currently registered with `declare_no_pointers()`. If the specified range is in an allocated object, then it is entirely within a single allocated object. The object is live until the corresponding `undeclare_no_pointers()` call. [*Note 2*: In a garbage-collecting implementation, the fact that a region in an object is registered with `declare_no_pointers()` should not prevent the object from being collected. — *end note*]

**10** *Effects:* The `n` bytes starting at `p` no longer contain traceable pointer locations, independent of their type. Hence indirection through a pointer located there is undefined if the object it points to was created by global `operator new` and not previously declared reachable. [*Note 3*: This may be used to inform a garbage collector or leak detector that this region of memory need not be traced. — *end note*]

**11** *Throws:* Nothing.

**12** [*Note 4*: Under some conditions implementations may need to allocate memory. However, the request can be ignored if memory allocation fails. — *end note*]

```cpp
void undeclare_no_pointers(char* p, size_t n);
```

**13** *Preconditions:* The same range has previously been passed to `declare_no_pointers()`.

**14** *Effects:* Unregisters a range registered with `declare_no_pointers()` for destruction. It shall be called before the lifetime of the object ends.

**15** *Throws:* Nothing.

```cpp
pointer_safety get_pointer_safety() noexcept;
```

**16** *Returns:* `pointer_safety::strict` if the implementation has strict pointer safety ([[basic.stc.dynamic.safety]](../../basic/basic.stc.dynamic.safety.md)). It is implementation-defined whether `get_pointer_safety` returns `pointer_safety::relaxed` or `pointer_safety::preferred` if the implementation has relaxed pointer safety.[^1]

[^1]: `pointer_safety::preferred` might be returned to indicate that a leak detector is running so that the program can avoid spurious leak reports.
