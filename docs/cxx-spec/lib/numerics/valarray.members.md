# 26.7.2.8 Member functions [valarray.members]

[Contents](../../README.md) · up: [26.7.2 Class template `valarray` \[template.valarray\]](template.valarray.md) · prev: [26.7.2.7 Compound assignment \[valarray.cassign\]](valarray.cassign.md) · next: [26.7.3 `valarray` non-member operations \[valarray.nonmembers\]](valarray.nonmembers.md)

```cpp
void swap(valarray& v) noexcept;
```

**1** *Effects:* `*this` obtains the value of `v`. `v` obtains the value of `*this`.

**2** *Complexity:* Constant.

```cpp
size_t size() const;
```

**3** *Returns:* The number of elements in the array.

**4** *Complexity:* Constant time.

```cpp
T sum() const;
```

**5** *Mandates:* `operator+=` can be applied to operands of type `T`.

**6** *Preconditions:* `size() > 0` is `true`.

**7** *Returns:* The sum of all the elements of the array. If the array has length 1, returns the value of element 0. Otherwise, the returned value is calculated by applying `operator+=` to a copy of an element of the array and all other elements of the array in an unspecified order.

```cpp
T min() const;
```

**8** *Preconditions:* `size() > 0` is `true`.

**9** *Returns:* The minimum value contained in `*this`. For an array of length 1, the value of element 0 is returned. For all other array lengths, the determination is made using `operator<`.

```cpp
T max() const;
```

**10** *Preconditions:* `size() > 0` is `true`.

**11** *Returns:* The maximum value contained in `*this`. For an array of length 1, the value of element 0 is returned. For all other array lengths, the determination is made using `operator<`.

```cpp
valarray shift(int n) const;
```

**12** *Returns:* A `valarray` of length `size()`, each of whose elements *I* is `(*this)[I + n]` if `I + n` is non-negative and less than `size()`, otherwise `T()`. [*Note 1*: If element zero is taken as the leftmost element, a positive value of `n` shifts the elements left `n` places, with zero fill. — *end note*]

**13** [*Example 1*: If the argument has the value -2, the first two elements of the result will be value-initialized ([[dcl.init]](../../dcl/dcl.init.md)); the third element of the result will be assigned the value of the first element of the argument; etc. — *end example*]

```cpp
valarray cshift(int n) const;
```

**14** *Returns:* A `valarray` of length `size()` that is a circular shift of `*this`. If element zero is taken as the leftmost element, a non-negative value of *n* shifts the elements circularly left *n* places and a negative value of *n* shifts the elements circularly right -*n* places.

```cpp
valarray apply(T func(T)) const;
valarray apply(T func(const T&)) const;
```

**15** *Returns:* A `valarray` whose length is `size()`. Each element of the returned array is assigned the value returned by applying the argument function to the corresponding element of `*this`.

```cpp
void resize(size_t sz, T c = T());
```

**16** *Effects:* Changes the length of the `*this` array to `sz` and then assigns to each element the value of the second argument. Resizing invalidates all pointers and references to elements in the array.
