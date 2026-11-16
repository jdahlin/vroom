# 20.11.1.2.3 Assignment [unique.ptr.single.asgn]

[Contents](../README.md) · up: [20.11.1.2 `unique_ptr` for single objects \[unique.ptr.single\]](unique.ptr.single.md) · prev: [20.11.1.2.2 Destructor \[unique.ptr.single.dtor\]](unique.ptr.single.dtor.md) · next: [20.11.1.2.4 Observers \[unique.ptr.single.observers\]](unique.ptr.single.observers.md)

```cpp
unique_ptr& operator=(unique_ptr&& u) noexcept;
```

**1** *Constraints:* `is_move_assignable_v<D>` is `true`.

**2** *Preconditions:* If `D` is not a reference type, `D` meets the *Cpp17MoveAssignable* requirements ([Table 30](utility.arg.requirements.md#tab:cpp17.moveassignable)) and assignment of the deleter from an rvalue of type `D` does not throw an exception. Otherwise, `D` is a reference type; `remove_reference_t<D>` meets the *Cpp17CopyAssignable* requirements and assignment of the deleter from an lvalue of type `D` does not throw an exception.

**3** *Effects:* Calls `reset(u.release())` followed by `get_deleter() = std::forward<D>(u.get_deleter())`.

**4** *Returns:* `*this`.

**5** *Postconditions:* `u.get() == nullptr`.

```cpp
template<class U, class E> unique_ptr& operator=(unique_ptr<U, E>&& u) noexcept;
```

**6** *Constraints:*

- `unique_ptr<U, E>::pointer` is implicitly convertible to `pointer`, and
- `U` is not an array type, and
- `is_assignable_v<D&, E&&>` is `true`.

**7** *Preconditions:* If `E` is not a reference type, assignment of the deleter from an rvalue of type `E` is well-formed and does not throw an exception. Otherwise, `E` is a reference type and assignment of the deleter from an lvalue of type `E` is well-formed and does not throw an exception.

**8** *Effects:* Calls `reset(u.release())` followed by `get_deleter() = std::forward<E>(u.get_deleter())`.

**9** *Returns:* `*this`.

**10** *Postconditions:* `u.get() == nullptr`.

```cpp
unique_ptr& operator=(nullptr_t) noexcept;
```

**11** *Effects:* As if by `reset()`.

**12** *Postconditions:* `get() == nullptr`.

**13** *Returns:* `*this`.
