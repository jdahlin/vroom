# 20.10.10.1 Members [allocator.members]

[Contents](../README.md) · up: [20.10.10 The default allocator \[default.allocator\]](default.allocator.md) · prev: [20.10.10 The default allocator \[default.allocator\]](default.allocator.md) · next: [20.10.10.2 Operators \[allocator.globals\]](allocator.globals.md)

**1** Except for the destructor, member functions of the default allocator shall not introduce data races ([[intro.multithread]](../intro.multithread.md)) as a result of concurrent calls to those member functions from different threads. Calls to these functions that allocate or deallocate a particular unit of storage shall occur in a single total order, and each such deallocation call shall happen before the next allocation (if any) in this order.

```cpp
[[nodiscard]] constexpr T* allocate(size_t n);
```

**2** *Mandates:* `T` is not an incomplete type ([[basic.types]](../basic.types.md)).

**3** *Returns:* A pointer to the initial element of an array of `n` `T`.

**4** *Remarks:* The storage for the array is obtained by calling `::operator new` ([[new.delete]](new.delete.md)), but it is unspecified when or how often this function is called. This function starts the lifetime of the array object, but not that of any of the array elements.

**5** *Throws:* `bad_array_new_length` if `numeric_limits<size_t>::max() / sizeof(T) < n`, or `bad_alloc` if the storage cannot be obtained.

```cpp
constexpr void deallocate(T* p, size_t n);
```

**6** *Preconditions:* `p` is a pointer value obtained from `allocate()`. `n` equals the value passed as the first argument to the invocation of allocate which returned `p`.

**7** *Effects:* Deallocates the storage referenced by `p`.

**8** *Remarks:* Uses `::operator delete` ([[new.delete]](new.delete.md)), but it is unspecified when this function is called.
