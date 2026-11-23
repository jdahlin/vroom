# 20.11.3.1 Constructors [util.smartptr.shared.const]

[Contents](../../README.md) · up: [20.11.3 Class template `shared_ptr` \[util.smartptr.shared\]](util.smartptr.shared.md) · prev: [20.11.3 Class template `shared_ptr` \[util.smartptr.shared\]](util.smartptr.shared.md) · next: [20.11.3.2 Destructor \[util.smartptr.shared.dest\]](util.smartptr.shared.dest.md)

**1** In the constructor definitions below, enables `shared_from_this` with `p`, for a pointer `p` of type `Y*`, means that if `Y` has an unambiguous and accessible base class that is a specialization of `enable_shared_from_this` ([[util.smartptr.enab]](util.smartptr.enab.md)), then `remove_cv_t<Y>*` shall be implicitly convertible to `T*` and the constructor evaluates the statement:

```cpp
if (p != nullptr && p->weak_this.expired())
  p->weak_this = shared_ptr<remove_cv_t<Y>>(*this, const_cast<remove_cv_t<Y>*>(p));
```

The assignment to the `weak_this` member is not atomic and conflicts with any potentially concurrent access to the same object ([[intro.multithread]](../../basic/intro.multithread.md)).

```cpp
constexpr shared_ptr() noexcept;
```

**2** *Postconditions:* `use_count() == 0 && get() == nullptr`.

```cpp
template<class Y> explicit shared_ptr(Y* p);
```

**3** *Mandates:* `Y` is a complete type.

**4** *Constraints:* When `T` is an array type, the expression `delete[] p` is well-formed and either `T` is `U[N]` and `Y(*)[N]` is convertible to `T*`, or `T` is `U[]` and `Y(*)[]` is convertible to `T*`. When `T` is not an array type, the expression `delete p` is well-formed and `Y*` is convertible to `T*`.

**5** *Preconditions:* The expression `delete[] p`, when `T` is an array type, or `delete p`, when `T` is not an array type, has well-defined behavior, and does not throw exceptions.

**6** *Effects:* When `T` is not an array type, constructs a `shared_ptr` object that owns the pointer `p`. Otherwise, constructs a `shared_ptr` that owns `p` and a deleter of an unspecified type that calls `delete[] p`. When `T` is not an array type, enables `shared_from_this` with `p`. If an exception is thrown, `delete p` is called when `T` is not an array type, `delete[] p` otherwise.

**7** *Postconditions:* `use_count() == 1 && get() == p`.

**8** *Throws:* `bad_alloc`, or an implementation-defined exception when a resource other than memory could not be obtained.

```cpp
template<class Y, class D> shared_ptr(Y* p, D d);
template<class Y, class D, class A> shared_ptr(Y* p, D d, A a);
template<class D> shared_ptr(nullptr_t p, D d);
template<class D, class A> shared_ptr(nullptr_t p, D d, A a);
```

**9** *Constraints:* `is_move_constructible_v<D>` is `true`, and `d(p)` is a well-formed expression. For the first two overloads:

- If `T` is an array type, then either `T` is `U[N]` and `Y(*)[N]` is convertible to `T*`, or `T` is `U[]` and `Y(*)[]` is convertible to `T*`.
- If `T` is not an array type, then `Y*` is convertible to `T*`.

**10** *Preconditions:* Construction of `d` and a deleter of type `D` initialized with `std::move(d)` do not throw exceptions. The expression `d(p)` has well-defined behavior and does not throw exceptions. `A` meets the *Cpp17Allocator* requirements ([Table 36](../library/allocator.requirements.md#tab:cpp17.allocator)).

**11** *Effects:* Constructs a `shared_ptr` object that owns the object `p` and the deleter `d`. When `T` is not an array type, the first and second constructors enable `shared_from_this` with `p`. The second and fourth constructors shall use a copy of `a` to allocate memory for internal use. If an exception is thrown, `d(p)` is called.

**12** *Postconditions:* `use_count() == 1 && get() == p`.

**13** *Throws:* `bad_alloc`, or an implementation-defined exception when a resource other than memory could not be obtained.

```cpp
template<class Y> shared_ptr(const shared_ptr<Y>& r, element_type* p) noexcept;
template<class Y> shared_ptr(shared_ptr<Y>&& r, element_type* p) noexcept;
```

**14** *Effects:* Constructs a `shared_ptr` instance that stores `p` and shares ownership with the initial value of `r`.

**15** *Postconditions:* `get() == p`. For the second overload, `r` is empty and `r.get() == nullptr`.

**16** [*Note 1*: To avoid the possibility of a dangling pointer, the user of this constructor should ensure that `p` remains valid at least until the ownership group of `r` is destroyed. — *end note*]

**17** [*Note 2*: This constructor allows creation of an empty `shared_ptr` instance with a non-null stored pointer. — *end note*]

```cpp
shared_ptr(const shared_ptr& r) noexcept;
template<class Y> shared_ptr(const shared_ptr<Y>& r) noexcept;
```

**18** *Constraints:* For the second constructor, `Y*` is compatible with `T*`.

**19** *Effects:* If `r` is empty, constructs an empty `shared_ptr` object; otherwise, constructs a `shared_ptr` object that shares ownership with `r`.

**20** *Postconditions:* `get() == r.get() && use_count() == r.use_count()`.

```cpp
shared_ptr(shared_ptr&& r) noexcept;
template<class Y> shared_ptr(shared_ptr<Y>&& r) noexcept;
```

**21** *Constraints:* For the second constructor, `Y*` is compatible with `T*`.

**22** *Effects:* Move constructs a `shared_ptr` instance from `r`.

**23** *Postconditions:* `*this` shall contain the old value of `r`. `r` shall be empty. `r.get() == nullptr`.

```cpp
template<class Y> explicit shared_ptr(const weak_ptr<Y>& r);
```

**24** *Constraints:* `Y*` is compatible with `T*`.

**25** *Effects:* Constructs a `shared_ptr` object that shares ownership with `r` and stores a copy of the pointer stored in `r`. If an exception is thrown, the constructor has no effect.

**26** *Postconditions:* `use_count() == r.use_count()`.

**27** *Throws:* `bad_weak_ptr` when `r.expired()`.

```cpp
template<class Y, class D> shared_ptr(unique_ptr<Y, D>&& r);
```

**28** *Constraints:* `Y*` is compatible with `T*` and `unique_ptr<Y, D>::pointer` is convertible to `element_type*`.

**29** *Effects:* If `r.get() == nullptr`, equivalent to `shared_ptr()`. Otherwise, if `D` is not a reference type, equivalent to `shared_ptr(r.release(), r.get_deleter())`. Otherwise, equivalent to `shared_ptr(r.release(), ref(r.get_deleter()))`. If an exception is thrown, the constructor has no effect.
