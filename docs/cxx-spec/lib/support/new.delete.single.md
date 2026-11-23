# 17.6.2.1 Single-object forms [new.delete.single]

[Contents](../../README.md) · up: [17.6.2 Storage allocation and deallocation \[new.delete\]](new.delete.md) · prev: [17.6.2 Storage allocation and deallocation \[new.delete\]](new.delete.md) · next: [17.6.2.2 Array forms \[new.delete.array\]](new.delete.array.md)

```cpp
[[nodiscard]] void* operator new(std::size_t size);
[[nodiscard]] void* operator new(std::size_t size, std::align_val_t alignment);
```

**1** *Effects:* The allocation functions ([[basic.stc.dynamic.allocation]](../../basic/basic.stc.dynamic.allocation.md)) called by a *new-expression* ([[expr.new]](../../expr/expr.new.md)) to allocate `size` bytes of storage. The second form is called for a type with new-extended alignment, and the first form is called otherwise.

**2** *Replaceable:* A C++ program may define functions with either of these function signatures, and thereby displace the default versions defined by the C++ standard library.

**3** *Required behavior:* Return a non-null pointer to suitably aligned storage ([[basic.stc.dynamic]](../../basic/basic.stc.dynamic.md)), or else throw a `bad_alloc` exception. This requirement is binding on any replacement versions of these functions.

**4** *Default behavior:*

- Executes a loop: Within the loop, the function first attempts to allocate the requested storage. Whether the attempt involves a call to the C standard library functions `malloc` or `aligned_alloc` is unspecified.
- Returns a pointer to the allocated storage if the attempt is successful. Otherwise, if the current `new_handler` ([[get.new.handler]](get.new.handler.md)) is a null pointer value, throws `bad_alloc`.
- Otherwise, the function calls the current `new_handler` function ([[new.handler]](new.handler.md)). If the called function returns, the loop repeats.
- The loop terminates when an attempt to allocate the requested storage is successful or when a called `new_handler` function does not return.

```cpp
[[nodiscard]] void* operator new(std::size_t size, const std::nothrow_t&) noexcept;
[[nodiscard]] void* operator new(std::size_t size, std::align_val_t alignment,
                                 const std::nothrow_t&) noexcept;
```

**5** *Effects:* Same as above, except that these are called by a placement version of a *new-expression* when a C++ program prefers a null pointer result as an error indication, instead of a `bad_alloc` exception.

**6** *Replaceable:* A C++ program may define functions with either of these function signatures, and thereby displace the default versions defined by the C++ standard library.

**7** *Required behavior:* Return a non-null pointer to suitably aligned storage ([[basic.stc.dynamic]](../../basic/basic.stc.dynamic.md)), or else return a null pointer. Each of these nothrow versions of `operator new` returns a pointer obtained as if acquired from the (possibly replaced) corresponding non-placement function. This requirement is binding on any replacement versions of these functions.

**8** *Default behavior:* Calls `operator new(size)`, or `operator new(size, alignment)`, respectively. If the call returns normally, returns the result of that call. Otherwise, returns a null pointer.

**9** [*Example 1*:

```cpp
T* p1 = new T;                  // throws bad_alloc if it fails
T* p2 = new(nothrow) T;         // returns nullptr if it fails
```

— *end example*]

```cpp
void operator delete(void* ptr) noexcept;
void operator delete(void* ptr, std::size_t size) noexcept;
void operator delete(void* ptr, std::align_val_t alignment) noexcept;
void operator delete(void* ptr, std::size_t size, std::align_val_t alignment) noexcept;
```

**10** *Effects:* The deallocation functions ([[basic.stc.dynamic.deallocation]](../../basic/basic.stc.dynamic.deallocation.md)) called by a *delete-expression* ([[expr.delete]](../../expr/expr.delete.md)) to render the value of `ptr` invalid.

**11** *Replaceable:* A C++ program may define functions with any of these function signatures, and thereby displace the default versions defined by the C++ standard library. If a function without a `size` parameter is defined, the program should also define the corresponding function with a `size` parameter. If a function with a `size` parameter is defined, the program shall also define the corresponding version without the `size` parameter. [*Note 1*: The default behavior below may change in the future, which will require replacing both deallocation functions when replacing the allocation function. — *end note*]

**12** *Preconditions:* `ptr` is a null pointer or its value represents the address of a block of memory allocated by an earlier call to a (possibly replaced) `operator new(std::size_t)` or `operator new(std::size_t, std::align_val_t)` which has not been invalidated by an intervening call to `operator delete`.

**13** *Preconditions:* If an implementation has strict pointer safety ([[basic.stc.dynamic.safety]](../../basic/basic.stc.dynamic.safety.md)) then `ptr` is a safely-derived pointer.

**14** *Preconditions:* If the `alignment` parameter is not present, `ptr` was returned by an allocation function without an `alignment` parameter. If present, the `alignment` argument is equal to the `alignment` argument passed to the allocation function that returned `ptr`. If present, the `size` argument is equal to the `size` argument passed to the allocation function that returned `ptr`.

**15** *Required behavior:* A call to an `operator delete` with a `size` parameter may be changed to a call to the corresponding `operator delete` without a `size` parameter, without affecting memory allocation. [*Note 2*: A conforming implementation is for `operator delete(void* ptr, std::size_t size)` to simply call `operator delete(ptr)`. — *end note*]

**16** *Default behavior:* The functions that have a `size` parameter forward their other parameters to the corresponding function without a `size` parameter. [*Note 3*: See the note in the above *Replaceable:* paragraph. — *end note*]

**17** *Default behavior:* If `ptr` is null, does nothing. Otherwise, reclaims the storage allocated by the earlier call to `operator new`.

**18** *Remarks:* It is unspecified under what conditions part or all of such reclaimed storage will be allocated by subsequent calls to `operator new` or any of `aligned_alloc`, `calloc`, `malloc`, or `realloc`, declared in `<cstdlib>` ([[cstdlib.syn]](cstdlib.syn.md)).

```cpp
void operator delete(void* ptr, const std::nothrow_t&) noexcept;
void operator delete(void* ptr, std::align_val_t alignment, const std::nothrow_t&) noexcept;
```

**19** *Effects:* The deallocation functions ([[basic.stc.dynamic.deallocation]](../../basic/basic.stc.dynamic.deallocation.md)) called by the implementation to render the value of `ptr` invalid when the constructor invoked from a nothrow placement version of the *new-expression* throws an exception.

**20** *Replaceable:* A C++ program may define functions with either of these function signatures, and thereby displace the default versions defined by the C++ standard library.

**21** *Preconditions:* `ptr` is a null pointer or its value represents the address of a block of memory allocated by an earlier call to a (possibly replaced) `operator new(std::size_t)` or `operator new(std::size_t, std::align_val_t)` which has not been invalidated by an intervening call to `operator delete`.

**22** *Preconditions:* If an implementation has strict pointer safety ([[basic.stc.dynamic.safety]](../../basic/basic.stc.dynamic.safety.md)) then `ptr` is a safely-derived pointer.

**23** *Preconditions:* If the `alignment` parameter is not present, `ptr` was returned by an allocation function without an `alignment` parameter. If present, the `alignment` argument is equal to the `alignment` argument passed to the allocation function that returned `ptr`.

**24** *Default behavior:* Calls `operator delete(ptr)`, or `operator delete(ptr, alignment)`, respectively.
