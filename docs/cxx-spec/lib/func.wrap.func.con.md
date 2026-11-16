# 20.14.16.2.1 Constructors and destructor [func.wrap.func.con]

[Contents](../README.md) · up: [20.14.16.2 Class template `function` \[func.wrap.func\]](func.wrap.func.md) · prev: [20.14.16.2 Class template `function` \[func.wrap.func\]](func.wrap.func.md) · next: [20.14.16.2.2 Modifiers \[func.wrap.func.mod\]](func.wrap.func.mod.md)

```cpp
function() noexcept;
```

**1** *Postconditions:* `!*this`.

```cpp
function(nullptr_t) noexcept;
```

**2** *Postconditions:* `!*this`.

```cpp
function(const function& f);
```

**3** *Postconditions:* `!*this` if `!f`; otherwise, `*this` targets a copy of `f.target()`.

**4** *Throws:* Nothing if `f`’s target is a specialization of `reference_wrapper` or a function pointer. Otherwise, may throw `bad_alloc` or any exception thrown by the copy constructor of the stored callable object. [*Note 1*: Implementations should avoid the use of dynamically allocated memory for small callable objects, for example, where `f`’s target is an object holding only a pointer or reference to an object and a member function pointer. — *end note*]

```cpp
function(function&& f) noexcept;
```

**5** *Postconditions:* If `!f`, `*this` has no target; otherwise, the target of `*this` is equivalent to the target of `f` before the construction, and `f` is in a valid state with an unspecified value.

**6** [*Note 2*: Implementations should avoid the use of dynamically allocated memory for small callable objects, for example, where `f`’s target is an object holding only a pointer or reference to an object and a member function pointer. — *end note*]

```cpp
template<class F> function(F f);
```

**7** *Constraints:* `F` is Lvalue-Callable ([[func.wrap.func]](func.wrap.func.md)) for argument types `ArgTypes...` and return type `R`.

**8** *Preconditions:* `F` meets the *Cpp17CopyConstructible* requirements.

**9** *Postconditions:* `!*this` if any of the following hold:

- `f` is a null function pointer value.
- `f` is a null member pointer value.
- `F` is an instance of the `function` class template, and `!f`.

**10** Otherwise, `*this` targets a copy of `f` initialized with `std::move(f)`. [*Note 3*: Implementations should avoid the use of dynamically allocated memory for small callable objects, for example, where `f` is an object holding only a pointer or reference to an object and a member function pointer. — *end note*]

**11** *Throws:* Nothing if `f` is a specialization of `reference_wrapper` or a function pointer. Otherwise, may throw `bad_alloc` or any exception thrown by `F`’s copy or move constructor.

```cpp
template<class F> function(F) -> function<see below>;
```

**12** *Constraints:* `&F::operator()` is well-formed when treated as an unevaluated operand and `decltype(&F::operator())` is of the form `R(G::*)(A...)` *cv* `&ₒₚₜ noexceptₒₚₜ` for a class type `G`.

**13** *Remarks:* The deduced type is `function<R(A...)>`.

**14** [*Example 1*:

```cpp
void f() {
  int i{5};
  function g = [&](double) { return i; };       // deduces function<int(double)>
}
```

— *end example*]

```cpp
function& operator=(const function& f);
```

**15** *Effects:* As if by `function(f).swap(*this);`

**16** *Returns:* `*this`.

```cpp
function& operator=(function&& f);
```

**17** *Effects:* Replaces the target of `*this` with the target of `f`.

**18** *Returns:* `*this`.

```cpp
function& operator=(nullptr_t) noexcept;
```

**19** *Effects:* If `*this != nullptr`, destroys the target of `this`.

**20** *Postconditions:* `!(*this)`.

**21** *Returns:* `*this`.

```cpp
template<class F> function& operator=(F&& f);
```

**22** *Constraints:* `decay_t<F>` is Lvalue-Callable ([[func.wrap.func]](func.wrap.func.md)) for argument types `ArgTypes...` and return type `R`.

**23** *Effects:* As if by: `function(std::forward<F>(f)).swap(*this);`

**24** *Returns:* `*this`.

```cpp
template<class F> function& operator=(reference_wrapper<F> f) noexcept;
```

**25** *Effects:* As if by: `function(f).swap(*this);`

**26** *Returns:* `*this`.

```cpp
~function();
```

**27** *Effects:* If `*this != nullptr`, destroys the target of `this`.
