# 26.7.2.2 Constructors [valarray.cons]

[Contents](../README.md) · up: [26.7.2 Class template `valarray` \[template.valarray\]](template.valarray.md) · prev: [26.7.2.1 Overview \[template.valarray.overview\]](template.valarray.overview.md) · next: [26.7.2.3 Assignment \[valarray.assign\]](valarray.assign.md)

```cpp
valarray();
```

**1** *Effects:* Constructs a `valarray` that has zero length.[^1]

```cpp
explicit valarray(size_t n);
```

**2** *Effects:* Constructs a `valarray` that has length `n`. Each element of the array is value-initialized ([[dcl.init]](../dcl.init.md)).

```cpp
valarray(const T& v, size_t n);
```

**3** *Effects:* Constructs a `valarray` that has length `n`. Each element of the array is initialized with `v`.

```cpp
valarray(const T* p, size_t n);
```

**4** *Preconditions:* `[p, p + n)` is a valid range.

*Effects:* Constructs a `valarray` that has length `n`. The values of the elements of the array are initialized with the first `n` values pointed to by the first argument.[^2]

```cpp
valarray(const valarray& v);
```

**5** *Effects:* Constructs a `valarray` that has the same length as `v`. The elements are initialized with the values of the corresponding elements of `v`.[^3]

```cpp
valarray(valarray&& v) noexcept;
```

**6** *Effects:* Constructs a `valarray` that has the same length as `v`. The elements are initialized with the values of the corresponding elements of `v`.

**7** *Complexity:* Constant.

```cpp
valarray(initializer_list<T> il);
```

**8** *Effects:* Equivalent to `valarray(il.begin(), il.size())`.

```cpp
valarray(const slice_array<T>&);
valarray(const gslice_array<T>&);
valarray(const mask_array<T>&);
valarray(const indirect_array<T>&);
```

**9** These conversion constructors convert one of the four reference templates to a `valarray`.

```cpp
~valarray();
```

**10** *Effects:* The destructor is applied to every element of `*this`; an implementation may return all allocated memory.

[^1]: This default constructor is essential, since arrays of `valarray` may be useful. After initialization, the length of an empty array can be increased with the `resize` member function.
[^2]: This constructor is the preferred method for converting a C array to a `valarray` object.
[^3]: This copy constructor creates a distinct array rather than an alias. Implementations in which arrays share storage are permitted, but they would need to implement a copy-on-reference mechanism to ensure that arrays are conceptually distinct.
