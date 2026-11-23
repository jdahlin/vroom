# 26.7.2.3 Assignment [valarray.assign]

[Contents](../../README.md) · up: [26.7.2 Class template `valarray` \[template.valarray\]](template.valarray.md) · prev: [26.7.2.2 Constructors \[valarray.cons\]](valarray.cons.md) · next: [26.7.2.4 Element access \[valarray.access\]](valarray.access.md)

```cpp
valarray& operator=(const valarray& v);
```

**1** *Effects:* Each element of the `*this` array is assigned the value of the corresponding element of `v`. If the length of `v` is not equal to the length of `*this`, resizes `*this` to make the two arrays the same length, as if by calling `resize(v.size())`, before performing the assignment.

**2** *Postconditions:* `size() == v.size()`.

**3** *Returns:* `*this`.

```cpp
valarray& operator=(valarray&& v) noexcept;
```

**4** *Effects:* `*this` obtains the value of `v`. The value of `v` after the assignment is not specified.

**5** *Returns:* `*this`.

**6** *Complexity:* Linear.

```cpp
valarray& operator=(initializer_list<T> il);
```

**7** *Effects:* Equivalent to: `return *this = valarray(il);`

```cpp
valarray& operator=(const T& v);
```

**8** *Effects:* Assigns `v` to each element of `*this`.

**9** *Returns:* `*this`.

```cpp
valarray& operator=(const slice_array<T>&);
valarray& operator=(const gslice_array<T>&);
valarray& operator=(const mask_array<T>&);
valarray& operator=(const indirect_array<T>&);
```

**10** *Preconditions:* The length of the array to which the argument refers equals `size()`. The value of an element in the left-hand side of a `valarray` assignment operator does not depend on the value of another element in that left-hand side.

**11** These operators allow the results of a generalized subscripting operation to be assigned directly to a `valarray`.
