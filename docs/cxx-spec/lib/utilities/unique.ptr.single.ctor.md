# 20.11.1.2.1 Constructors [unique.ptr.single.ctor]

[Contents](../../README.md) · up: [20.11.1.2 `unique_ptr` for single objects \[unique.ptr.single\]](unique.ptr.single.md) · prev: [20.11.1.2 `unique_ptr` for single objects \[unique.ptr.single\]](unique.ptr.single.md) · next: [20.11.1.2.2 Destructor \[unique.ptr.single.dtor\]](unique.ptr.single.dtor.md)

```cpp
constexpr unique_ptr() noexcept;
constexpr unique_ptr(nullptr_t) noexcept;
```

**1** *Preconditions:* `D` meets the *Cpp17DefaultConstructible* requirements ([Table 27](../library/utility.arg.requirements.md#tab:cpp17.defaultconstructible)), and that construction does not throw an exception.

**2** *Constraints:* `is_pointer_v<deleter_type>` is `false` and `is_default_constructible_v<deleter_type>` is `true`.

**3** *Effects:* Constructs a `unique_ptr` object that owns nothing, value-initializing the stored pointer and the stored deleter.

**4** *Postconditions:* `get() == nullptr`. `get_deleter()` returns a reference to the stored deleter.

```cpp
explicit unique_ptr(pointer p) noexcept;
```

**5** *Constraints:* `is_pointer_v<deleter_type>` is `false` and `is_default_constructible_v<deleter_type>` is `true`.

**6** *Mandates:* This constructor is not selected by class template argument deduction ([[over.match.class.deduct]](../../over/over.match.class.deduct.md)).

**7** *Preconditions:* `D` meets the *Cpp17DefaultConstructible* requirements ([Table 27](../library/utility.arg.requirements.md#tab:cpp17.defaultconstructible)), and that construction does not throw an exception.

**8** *Effects:* Constructs a `unique_ptr` which owns `p`, initializing the stored pointer with `p` and value-initializing the stored deleter.

**9** *Postconditions:* `get() == p`. `get_deleter()` returns a reference to the stored deleter.

```cpp
unique_ptr(pointer p, const D& d) noexcept;
unique_ptr(pointer p, remove_reference_t<D>&& d) noexcept;
```

**10** *Constraints:* `is_constructible_v<D, decltype(d)>` is `true`.

**11** *Mandates:* These constructors are not selected by class template argument deduction ([[over.match.class.deduct]](../../over/over.match.class.deduct.md)).

**12** *Preconditions:* For the first constructor, if `D` is not a reference type, `D` meets the *Cpp17CopyConstructible* requirements and such construction does not exit via an exception. For the second constructor, if `D` is not a reference type, `D` meets the *Cpp17MoveConstructible* requirements and such construction does not exit via an exception.

**13** *Effects:* Constructs a `unique_ptr` object which owns `p`, initializing the stored pointer with `p` and initializing the deleter from `std::forward<decltype(d)>(d)`.

**14** *Postconditions:* `get() == p`. `get_deleter()` returns a reference to the stored deleter. If `D` is a reference type then `get_deleter()` returns a reference to the lvalue `d`.

**15** *Remarks:* If `D` is a reference type, the second constructor is defined as deleted.

**16** [*Example 1*:

```cpp
D d;
unique_ptr<int, D> p1(new int, D());        // D must be Cpp17MoveConstructible
unique_ptr<int, D> p2(new int, d);          // D must be Cpp17CopyConstructible
unique_ptr<int, D&> p3(new int, d);         // p3 holds a reference to d
unique_ptr<int, const D&> p4(new int, D()); // error: rvalue deleter object combined
                                            // with reference deleter type
```

— *end example*]

```cpp
unique_ptr(unique_ptr&& u) noexcept;
```

**17** *Constraints:* `is_move_constructible_v<D>` is `true`.

**18** *Preconditions:* If `D` is not a reference type, `D` meets the *Cpp17MoveConstructible* requirements ([Table 28](../library/utility.arg.requirements.md#tab:cpp17.moveconstructible)). Construction of the deleter from an rvalue of type `D` does not throw an exception.

**19** *Effects:* Constructs a `unique_ptr` from `u`. If `D` is a reference type, this deleter is copy constructed from `u`’s deleter; otherwise, this deleter is move constructed from `u`’s deleter. [*Note 1*: The construction of the deleter can be implemented with `std::forward<D>`. — *end note*]

**20** *Postconditions:* `get()` yields the value `u.get()` yielded before the construction. `u.get() == nullptr`. `get_deleter()` returns a reference to the stored deleter that was constructed from `u.get_deleter()`. If `D` is a reference type then `get_deleter()` and `u.get_deleter()` both reference the same lvalue deleter.

```cpp
template<class U, class E> unique_ptr(unique_ptr<U, E>&& u) noexcept;
```

**21** *Constraints:*

- `unique_ptr<U, E>::pointer` is implicitly convertible to `pointer`,
- `U` is not an array type, and
- either `D` is a reference type and `E` is the same type as `D`, or `D` is not a reference type and `E` is implicitly convertible to `D`.

**22** *Preconditions:* If `E` is not a reference type, construction of the deleter from an rvalue of type `E` is well-formed and does not throw an exception. Otherwise, `E` is a reference type and construction of the deleter from an lvalue of type `E` is well-formed and does not throw an exception.

**23** *Effects:* Constructs a `unique_ptr` from `u`. If `E` is a reference type, this deleter is copy constructed from `u`’s deleter; otherwise, this deleter is move constructed from `u`’s deleter. [*Note 2*: The deleter constructor can be implemented with `std::forward<E>`. — *end note*]

**24** *Postconditions:* `get()` yields the value `u.get()` yielded before the construction. `u.get() == nullptr`. `get_deleter()` returns a reference to the stored deleter that was constructed from `u.get_deleter()`.
