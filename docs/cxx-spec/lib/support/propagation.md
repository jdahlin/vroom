# 17.9.6 Exception propagation [propagation]

[Contents](../../README.md) · up: [17.9 Exception handling \[support.exception\]](support.exception.md) · prev: [17.9.5 `uncaught_exceptions` \[uncaught.exceptions\]](uncaught.exceptions.md) · next: [17.9.7 `nested_exception` \[except.nested\]](except.nested.md)

```cpp
using exception_ptr = unspecified;
```

**1** The type `exception_ptr` can be used to refer to an exception object.

**2** `exception_ptr` meets the requirements of *Cpp17NullablePointer* ([Table 33](../library/nullablepointer.requirements.md#tab:cpp17.nullablepointer)).

**3** Two non-null values of type `exception_ptr` are equivalent and compare equal if and only if they refer to the same exception.

**4** The default constructor of `exception_ptr` produces the null value of the type.

**5** `exception_ptr` shall not be implicitly convertible to any arithmetic, enumeration, or pointer type.

**6** [*Note 1*: An implementation might use a reference-counted smart pointer as `exception_ptr`. — *end note*]

**7** For purposes of determining the presence of a data race, operations on `exception_ptr` objects shall access and modify only the `exception_ptr` objects themselves and not the exceptions they refer to. Use of `rethrow_exception` on `exception_ptr` objects that refer to the same exception object shall not introduce a data race. [*Note 2*: If `rethrow_exception` rethrows the same exception object (rather than a copy), concurrent access to that rethrown exception object may introduce a data race. Changes in the number of `exception_ptr` objects that refer to a particular exception do not introduce a data race. — *end note*]

```cpp
exception_ptr current_exception() noexcept;
```

**8** *Returns:* An `exception_ptr` object that refers to the currently handled exception ([[except.handle]](../../except/except.handle.md)) or a copy of the currently handled exception, or a null `exception_ptr` object if no exception is being handled. The referenced object shall remain valid at least as long as there is an `exception_ptr` object that refers to it. If the function needs to allocate memory and the attempt fails, it returns an `exception_ptr` object that refers to an instance of `bad_alloc`. It is unspecified whether the return values of two successive calls to `current_exception` refer to the same exception object. [*Note 3*: That is, it is unspecified whether `current_exception` creates a new copy each time it is called. — *end note*] If the attempt to copy the current exception object throws an exception, the function returns an `exception_ptr` object that refers to the thrown exception or, if this is not possible, to an instance of `bad_exception`. [*Note 4*: The copy constructor of the thrown exception may also fail, so the implementation is allowed to substitute a `bad_exception` object to avoid infinite recursion. — *end note*]

```cpp
[[noreturn]] void rethrow_exception(exception_ptr p);
```

**9** *Preconditions:* `p` is not a null pointer.

**10** *Throws:* The exception object to which `p` refers.

```cpp
template<class E> exception_ptr make_exception_ptr(E e) noexcept;
```

**11** *Effects:* Creates an `exception_ptr` object that refers to a copy of `e`, as if:

```cpp
try {
  throw e;
} catch(...) {
  return current_exception();
}
```

**12** [*Note 5*: This function is provided for convenience and efficiency reasons. — *end note*]
