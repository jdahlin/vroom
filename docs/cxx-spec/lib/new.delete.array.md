# 17.6.2.2 Array forms [new.delete.array]

[Contents](../README.md) · up: [17.6.2 Storage allocation and deallocation \[new.delete\]](new.delete.md) · prev: [17.6.2.1 Single-object forms \[new.delete.single\]](new.delete.single.md) · next: [17.6.2.3 Non-allocating forms \[new.delete.placement\]](new.delete.placement.md)

```cpp
[[nodiscard]] void* operator new[](std::size_t size);
[[nodiscard]] void* operator new[](std::size_t size, std::align_val_t alignment);
```

**1** *Effects:* The allocation functions ([[basic.stc.dynamic.allocation]](../basic.stc.dynamic.allocation.md)) called by the array form of a *new-expression* ([[expr.new]](../expr.new.md)) to allocate `size` bytes of storage. The second form is called for a type with new-extended alignment, and the first form is called otherwise.[^1]

**2** *Replaceable:* A C++ program may define functions with either of these function signatures, and thereby displace the default versions defined by the C++ standard library.

**3** *Required behavior:* Same as for the corresponding single-object forms. This requirement is binding on any replacement versions of these functions.

**4** *Default behavior:* Returns `operator new(size)`, or `operator new(size, alignment)`, respectively.

```cpp
[[nodiscard]] void* operator new[](std::size_t size, const std::nothrow_t&) noexcept;
[[nodiscard]] void* operator new[](std::size_t size, std::align_val_t alignment,
                                   const std::nothrow_t&) noexcept;
```

**5** *Effects:* Same as above, except that these are called by a placement version of a *new-expression* when a C++ program prefers a null pointer result as an error indication, instead of a `bad_alloc` exception.

**6** *Replaceable:* A C++ program may define functions with either of these function signatures, and thereby displace the default versions defined by the C++ standard library.

**7** *Required behavior:* Return a non-null pointer to suitably aligned storage ([[basic.stc.dynamic]](../basic.stc.dynamic.md)), or else return a null pointer. Each of these nothrow versions of `operator new[]` returns a pointer obtained as if acquired from the (possibly replaced) corresponding non-placement function. This requirement is binding on any replacement versions of these functions.

**8** *Default behavior:* Calls `operator new[](size)`, or `operator new[](size, alignment)`, respectively. If the call returns normally, returns the result of that call. Otherwise, returns a null pointer.

```cpp
void operator delete[](void* ptr) noexcept;
void operator delete[](void* ptr, std::size_t size) noexcept;
void operator delete[](void* ptr, std::align_val_t alignment) noexcept;
void operator delete[](void* ptr, std::size_t size, std::align_val_t alignment) noexcept;
```

**9** *Effects:* The deallocation functions ([[basic.stc.dynamic.deallocation]](../basic.stc.dynamic.deallocation.md)) called by the array form of a *delete-expression* to render the value of `ptr` invalid.

**10** *Replaceable:* A C++ program may define functions with any of these function signatures, and thereby displace the default versions defined by the C++ standard library. If a function without a `size` parameter is defined, the program should also define the corresponding function with a `size` parameter. If a function with a `size` parameter is defined, the program shall also define the corresponding version without the `size` parameter. [*Note 1*: The default behavior below may change in the future, which will require replacing both deallocation functions when replacing the allocation function. — *end note*]

**11** *Preconditions:* `ptr` is a null pointer or its value represents the address of a block of memory allocated by an earlier call to a (possibly replaced) `operator new[](std::size_t)` or `operator new[](std::size_t, std::align_val_t)` which has not been invalidated by an intervening call to `operator delete[]`.

**12** *Preconditions:* If an implementation has strict pointer safety ([[basic.stc.dynamic.safety]](../basic.stc.dynamic.safety.md)) then `ptr` is a safely-derived pointer.

**13** *Preconditions:* If the `alignment` parameter is not present, `ptr` was returned by an allocation function without an `alignment` parameter. If present, the `alignment` argument is equal to the `alignment` argument passed to the allocation function that returned `ptr`. If present, the `size` argument is equal to the `size` argument passed to the allocation function that returned `ptr`.

**14** *Required behavior:* A call to an `operator delete[]` with a `size` parameter may be changed to a call to the corresponding `operator delete[]` without a `size` parameter, without affecting memory allocation. [*Note 2*: A conforming implementation is for `operator delete[](void* ptr, std::size_t size)` to simply call `operator delete[](ptr)`. — *end note*]

**15** *Default behavior:* The functions that have a `size` parameter forward their other parameters to the corresponding function without a `size` parameter. The functions that do not have a `size` parameter forward their parameters to the corresponding `operator delete` (single-object) function.

```cpp
void operator delete[](void* ptr, const std::nothrow_t&) noexcept;
void operator delete[](void* ptr, std::align_val_t alignment, const std::nothrow_t&) noexcept;
```

**16** *Effects:* The deallocation functions ([[basic.stc.dynamic.deallocation]](../basic.stc.dynamic.deallocation.md)) called by the implementation to render the value of `ptr` invalid when the constructor invoked from a nothrow placement version of the array *new-expression* throws an exception.

**17** *Replaceable:* A C++ program may define functions with either of these function signatures, and thereby displace the default versions defined by the C++ standard library.

**18** *Preconditions:* `ptr` is a null pointer or its value represents the address of a block of memory allocated by an earlier call to a (possibly replaced) `operator new[](std::size_t)` or `operator new[](std::size_t, std::align_val_t)` which has not been invalidated by an intervening call to `operator delete[]`.

**19** *Preconditions:* If an implementation has strict pointer safety ([[basic.stc.dynamic.safety]](../basic.stc.dynamic.safety.md)) then `ptr` is a safely-derived pointer.

**20** *Preconditions:* If the `alignment` parameter is not present, `ptr` was returned by an allocation function without an `alignment` parameter. If present, the `alignment` argument is equal to the `alignment` argument passed to the allocation function that returned `ptr`.

**21** *Default behavior:* Calls `operator delete[](ptr)`, or `operator delete[](ptr, alignment)`, respectively.

[^1]: It is not the direct responsibility of `operator new[]` or `operator delete[]` to note the repetition count or element size of the array. Those operations are performed elsewhere in the array `new` and `delete` expressions. The array `new` expression, may, however, increase the `size` argument to `operator new[]` to obtain space to store supplemental information.
