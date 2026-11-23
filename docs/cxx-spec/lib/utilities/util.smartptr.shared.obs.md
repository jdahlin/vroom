# 20.11.3.5 Observers [util.smartptr.shared.obs]

[Contents](../../README.md) · up: [20.11.3 Class template `shared_ptr` \[util.smartptr.shared\]](util.smartptr.shared.md) · prev: [20.11.3.4 Modifiers \[util.smartptr.shared.mod\]](util.smartptr.shared.mod.md) · next: [20.11.3.6 Creation \[util.smartptr.shared.create\]](util.smartptr.shared.create.md)

```cpp
element_type* get() const noexcept;
```

**1** *Returns:* The stored pointer.

```cpp
T& operator*() const noexcept;
```

**2** *Preconditions:* `get() != 0`.

**3** *Returns:* `*get()`.

**4** *Remarks:* When `T` is an array type or *cv* `void`, it is unspecified whether this member function is declared. If it is declared, it is unspecified what its return type is, except that the declaration (although not necessarily the definition) of the function shall be well-formed.

```cpp
T* operator->() const noexcept;
```

**5** *Preconditions:* `get() != 0`.

**6** *Returns:* `get()`.

**7** *Remarks:* When `T` is an array type, it is unspecified whether this member function is declared. If it is declared, it is unspecified what its return type is, except that the declaration (although not necessarily the definition) of the function shall be well-formed.

```cpp
element_type& operator[](ptrdiff_t i) const;
```

**8** *Preconditions:* `get() != 0 && i >= 0`. If `T` is `U[N]`, `i < N`.

**9** *Returns:* `get()[i]`.

**10** *Remarks:* When `T` is not an array type, it is unspecified whether this member function is declared. If it is declared, it is unspecified what its return type is, except that the declaration (although not necessarily the definition) of the function shall be well-formed.

**11** *Throws:* Nothing.

```cpp
long use_count() const noexcept;
```

**12** *Returns:* The number of `shared_ptr` objects, `*this` included, that share ownership with `*this`, or `0` when `*this` is empty.

**13** *Synchronization:* None.

**14** [*Note 1*: `get() == nullptr` does not imply a specific return value of `use_count()`. — *end note*]

**15** [*Note 2*: `weak_ptr<T>::lock()` can affect the return value of `use_count()`. — *end note*]

**16** [*Note 3*: When multiple threads can affect the return value of `use_count()`, the result should be treated as approximate. In particular, `use_count() == 1` does not imply that accesses through a previously destroyed `shared_ptr` have in any sense completed. — *end note*]

```cpp
explicit operator bool() const noexcept;
```

**17** *Returns:* `get() != 0`.

```cpp
template<class U> bool owner_before(const shared_ptr<U>& b) const noexcept;
template<class U> bool owner_before(const weak_ptr<U>& b) const noexcept;
```

**18** *Returns:* An unspecified value such that

- `x.owner_before(y)` defines a strict weak ordering as defined in [[alg.sorting]](../algorithms/alg.sorting.md);
- under the equivalence relation defined by `owner_before`, `!a.owner_before(b) && !b.owner_before(a)`, two `shared_ptr` or `weak_ptr` instances are equivalent if and only if they share ownership or are both empty.
