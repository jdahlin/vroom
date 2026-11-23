# 20.11.3.6 Creation [util.smartptr.shared.create]

[Contents](../../README.md) · up: [20.11.3 Class template `shared_ptr` \[util.smartptr.shared\]](util.smartptr.shared.md) · prev: [20.11.3.5 Observers \[util.smartptr.shared.obs\]](util.smartptr.shared.obs.md) · next: [20.11.3.7 Comparison \[util.smartptr.shared.cmp\]](util.smartptr.shared.cmp.md)

**1** The common requirements that apply to all `make_shared`, `allocate_shared`, `make_shared_for_overwrite`, and `allocate_shared_for_overwrite` overloads, unless specified otherwise, are described below.

```cpp
template<class T, ...>
  shared_ptr<T> make_shared(args);
template<class T, class A, ...>
  shared_ptr<T> allocate_shared(const A& a, args);
template<class T, ...>
  shared_ptr<T> make_shared_for_overwrite(args);
template<class T, class A, ...>
  shared_ptr<T> allocate_shared_for_overwrite(const A& a, args);
```

**2** *Preconditions:* `A` meets the *Cpp17Allocator* requirements ([Table 36](../library/allocator.requirements.md#tab:cpp17.allocator)).

**3** *Effects:* Allocates memory for an object of type `T` (or `U[N]` when `T` is `U[]`, where `N` is determined from *args* as specified by the concrete overload). The object is initialized from *args* as specified by the concrete overload. The `allocate_shared` and `allocate_shared_for_overwrite` templates use a copy of `a` (rebound for an unspecified `value_type`) to allocate memory. If an exception is thrown, the functions have no effect.

**4** *Returns:* A `shared_ptr` instance that stores and owns the address of the newly constructed object.

**5** *Postconditions:* `r.get() != 0 && r.use_count() == 1`, where `r` is the return value.

**6** *Throws:* `bad_alloc`, or an exception thrown from `allocate` or from the initialization of the object.

**7** *Remarks:*

- Implementations should perform no more than one memory allocation. [*Note 1*: This provides efficiency equivalent to an intrusive smart pointer. — *end note*]

- When an object of an array type `U` is specified to have an initial value of `u` (of the same type), this shall be interpreted to mean that each array element of the object has as its initial value the corresponding element from `u`.

- When an object of an array type is specified to have a default initial value, this shall be interpreted to mean that each array element of the object has a default initial value.

- When a (sub)object of a non-array type `U` is specified to have an initial value of `v`, or `U(l...)`, where `l...` is a list of constructor arguments, `make_shared` shall initialize this (sub)object via the expression `::new(pv) U(v)` or `::new(pv) U(l...)` respectively, where `pv` has type `void*` and points to storage suitable to hold an object of type `U`.

- When a (sub)object of a non-array type `U` is specified to have an initial value of `v`, or `U(l...)`, where `l...` is a list of constructor arguments, `allocate_shared` shall initialize this (sub)object via the expression

  - `allocator_traits<A2>::construct(a2, pv, v)` or
  - `allocator_traits<A2>::construct(a2, pv, l...)`

  respectively, where `pv` points to storage suitable to hold an object of type `U` and `a2` of type `A2` is a rebound copy of the allocator `a` passed to `allocate_shared` such that its `value_type` is `remove_cv_t<U>`.

- When a (sub)object of non-array type `U` is specified to have a default initial value, `make_shared` shall initialize this (sub)object via the expression `::new(pv) U()`, where `pv` has type `void*` and points to storage suitable to hold an object of type `U`.

- When a (sub)object of non-array type `U` is specified to have a default initial value, `allocate_shared` shall initialize this (sub)object via the expression `allocator_traits<A2>::construct(a2, pv)`, where `pv` points to storage suitable to hold an object of type `U` and `a2` of type `A2` is a rebound copy of the allocator `a` passed to `allocate_shared` such that its `value_type` is `remove_cv_t<U>`.

- When a (sub)object of non-array type `U` is initialized by `make_shared_for_overwrite` or`allocate_shared_for_overwrite`, it is initialized via the expression `::new(pv) U`, where `pv` has type `void*` and points to storage suitable to hold an object of type `U`.

- Array elements are initialized in ascending order of their addresses.

- When the lifetime of the object managed by the return value ends, or when the initialization of an array element throws an exception, the initialized elements are destroyed in the reverse order of their original construction.

- When a (sub)object of non-array type `U` that was initialized by `make_shared` is to be destroyed, it is destroyed via the expression `pv->~U()` where `pv` points to that object of type `U`.

- When a (sub)object of non-array type `U` that was initialized by `allocate_shared` is to be destroyed, it is destroyed via the expression `allocator_traits<A2>::destroy(a2, pv)` where `pv` points to that object of type `remove_cv_t<U>` and `a2` of type `A2` is a rebound copy of the allocator `a` passed to `allocate_shared` such that its `value_type` is `remove_cv_t<U>`.

[*Note 2*: These functions will typically allocate more memory than `sizeof(T)` to allow for internal bookkeeping structures such as reference counts. — *end note*]

```cpp
template<class T, class... Args>
  shared_ptr<T> make_shared(Args&&... args);                    // T is not array
template<class T, class A, class... Args>
  shared_ptr<T> allocate_shared(const A& a, Args&&... args);    // T is not array
```

**8** *Constraints:* `T` is not an array type.

**9** *Returns:* A `shared_ptr` to an object of type `T` with an initial value `T(forward<Args>(args)...)`.

**10** *Remarks:* The `shared_ptr` constructors called by these functions enable `shared_from_this` with the address of the newly constructed object of type `T`.

**11** [*Example 1*:

```cpp
shared_ptr<int> p = make_shared<int>(); // shared_ptr to int()
shared_ptr<vector<int>> q = make_shared<vector<int>>(16, 1);
  // shared_ptr to vector of 16 elements with value 1
```

— *end example*]

```cpp
template<class T> shared_ptr<T>
  make_shared(size_t N);                                        // T is U[]
template<class T, class A>
  shared_ptr<T> allocate_shared(const A& a, size_t N);          // T is U[]
```

**12** *Constraints:* `T` is of the form `U[]`.

**13** *Returns:* A `shared_ptr` to an object of type `U[N]` with a default initial value, where `U` is `remove_extent_t<T>`.

**14** [*Example 2*:

```cpp
shared_ptr<double[]> p = make_shared<double[]>(1024);
  // shared_ptr to a value-initialized double[1024]
shared_ptr<double[][2][2]> q = make_shared<double[][2][2]>(6);
  // shared_ptr to a value-initialized double[6][2][2]
```

— *end example*]

```cpp
template<class T>
  shared_ptr<T> make_shared();                                  // T is U[N]
template<class T, class A>
  shared_ptr<T> allocate_shared(const A& a);                    // T is U[N]
```

**15** *Constraints:* `T` is of the form `U[N]`.

**16** *Returns:* A `shared_ptr` to an object of type `T` with a default initial value.

**17** [*Example 3*:

```cpp
shared_ptr<double[1024]> p = make_shared<double[1024]>();
  // shared_ptr to a value-initialized double[1024]
shared_ptr<double[6][2][2]> q = make_shared<double[6][2][2]>();
  // shared_ptr to a value-initialized double[6][2][2]
```

— *end example*]

```cpp
template<class T>
  shared_ptr<T> make_shared(size_t N,
                            const remove_extent_t<T>& u);       // T is U[]
template<class T, class A>
  shared_ptr<T> allocate_shared(const A& a, size_t N,
                                const remove_extent_t<T>& u);   // T is U[]
```

**18** *Constraints:* `T` is of the form `U[]`.

**19** *Returns:* A `shared_ptr` to an object of type `U[N]`, where `U` is `remove_extent_t<T>` and each array element has an initial value of `u`.

**20** [*Example 4*:

```cpp
shared_ptr<double[]> p = make_shared<double[]>(1024, 1.0);
  // shared_ptr to a double[1024], where each element is 1.0
shared_ptr<double[][2]> q = make_shared<double[][2]>(6, {1.0, 0.0});
  // shared_ptr to a double[6][2], where each double[2] element is {1.0, 0.0}
shared_ptr<vector<int>[]> r = make_shared<vector<int>[]>(4, {1, 2});
  // shared_ptr to a vector<int>[4], where each vector has contents {1, 2}
```

— *end example*]

```cpp
template<class T>
  shared_ptr<T> make_shared(const remove_extent_t<T>& u);       // T is U[N]
template<class T, class A>
  shared_ptr<T> allocate_shared(const A& a,
                                const remove_extent_t<T>& u);   // T is U[N]
```

**21** *Constraints:* `T` is of the form `U[N]`.

**22** *Returns:* A `shared_ptr` to an object of type `T`, where each array element of type `remove_extent_t<T>` has an initial value of `u`.

**23** [*Example 5*:

```cpp
shared_ptr<double[1024]> p = make_shared<double[1024]>(1.0);
  // shared_ptr to a double[1024], where each element is 1.0
shared_ptr<double[6][2]> q = make_shared<double[6][2]>({1.0, 0.0});
  // shared_ptr to a double[6][2], where each double[2] element is {1.0, 0.0}
shared_ptr<vector<int>[4]> r = make_shared<vector<int>[4]>({1, 2});
  // shared_ptr to a vector<int>[4], where each vector has contents {1, 2}
```

— *end example*]

```cpp
template<class T>
  shared_ptr<T> make_shared_for_overwrite();
template<class T, class A>
  shared_ptr<T> allocate_shared_for_overwrite(const A& a);
```

**24** *Constraints:* `T` is not an array of unknown bound.

**25** *Returns:* A `shared_ptr` to an object of type `T`.

**26** [*Example 6*:

```cpp
struct X { double data[1024]; };
shared_ptr<X> p = make_shared_for_overwrite<X>();
  // shared_ptr to a default-initialized X, where each element in X::data has an indeterminate value

shared_ptr<double[1024]> q = make_shared_for_overwrite<double[1024]>();
  // shared_ptr to a default-initialized double[1024], where each element has an indeterminate value
```

— *end example*]

```cpp
template<class T>
  shared_ptr<T> make_shared_for_overwrite(size_t N);
template<class T, class A>
  shared_ptr<T> allocate_shared_for_overwrite(const A& a, size_t N);
```

**27** *Constraints:* `T` is an array of unknown bound.

**28** *Returns:* A `shared_ptr` to an object of type `U[N]`, where `U` is `remove_extent_t<T>`.

**29** [*Example 7*:

```cpp
shared_ptr<double[]> p = make_shared_for_overwrite<double[]>(1024);
  // shared_ptr to a default-initialized double[1024], where each element has an indeterminate value
```

— *end example*]
