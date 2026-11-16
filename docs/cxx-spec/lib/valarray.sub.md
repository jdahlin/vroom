# 26.7.2.5 Subset operations [valarray.sub]

[Contents](../README.md) · up: [26.7.2 Class template `valarray` \[template.valarray\]](template.valarray.md) · prev: [26.7.2.4 Element access \[valarray.access\]](valarray.access.md) · next: [26.7.2.6 Unary operators \[valarray.unary\]](valarray.unary.md)

**1** The member `operator[]` is overloaded to provide several ways to select sequences of elements from among those controlled by `*this`. Each of these operations returns a subset of the array. The const-qualified versions return this subset as a new `valarray` object. The non-const versions return a class template object which has reference semantics to the original array, working in conjunction with various overloads of `operator=` and other assigning operators to allow selective replacement (slicing) of the controlled sequence. In each case the selected element(s) shall exist.

```cpp
valarray operator[](slice slicearr) const;
```

**2** *Returns:* A `valarray` containing those elements of the controlled sequence designated by `slicearr`. [*Example 1*:

```cpp
const valarray<char> v0("abcdefghijklmnop", 16);
// v0[slice(2, 5, 3)] returns valarray<char>("cfilo", 5)
```

— *end example*]

```cpp
slice_array<T> operator[](slice slicearr);
```

**3** *Returns:* An object that holds references to elements of the controlled sequence selected by `slicearr`. [*Example 2*:

```cpp
valarray<char> v0("abcdefghijklmnop", 16);
valarray<char> v1("ABCDE", 5);
v0[slice(2, 5, 3)] = v1;
// v0 == valarray<char>("abAdeBghCjkDmnEp", 16);
```

— *end example*]

```cpp
valarray operator[](const gslice& gslicearr) const;
```

**4** *Returns:* A `valarray` containing those elements of the controlled sequence designated by `gslicearr`. [*Example 3*:

```cpp
const valarray<char> v0("abcdefghijklmnop", 16);
const size_t lv[] = { 2, 3 };
const size_t dv[] = { 7, 2 };
const valarray<size_t> len(lv, 2), str(dv, 2);
// v0[gslice(3, len, str)] returns
// valarray<char>("dfhkmo", 6)
```

— *end example*]

```cpp
gslice_array<T> operator[](const gslice& gslicearr);
```

**5** *Returns:* An object that holds references to elements of the controlled sequence selected by `gslicearr`. [*Example 4*:

```cpp
valarray<char> v0("abcdefghijklmnop", 16);
valarray<char> v1("ABCDEF", 6);
const size_t lv[] = { 2, 3 };
const size_t dv[] = { 7, 2 };
const valarray<size_t> len(lv, 2), str(dv, 2);
v0[gslice(3, len, str)] = v1;
// v0 == valarray<char>("abcAeBgCijDlEnFp", 16)
```

— *end example*]

```cpp
valarray operator[](const valarray<bool>& boolarr) const;
```

**6** *Returns:* A `valarray` containing those elements of the controlled sequence designated by `boolarr`. [*Example 5*:

```cpp
const valarray<char> v0("abcdefghijklmnop", 16);
const bool vb[] = { false, false, true, true, false, true };
// v0[valarray<bool>(vb, 6)] returns
// valarray<char>("cdf", 3)
```

— *end example*]

```cpp
mask_array<T> operator[](const valarray<bool>& boolarr);
```

**7** *Returns:* An object that holds references to elements of the controlled sequence selected by `boolarr`. [*Example 6*:

```cpp
valarray<char> v0("abcdefghijklmnop", 16);
valarray<char> v1("ABC", 3);
const bool vb[] = { false, false, true, true, false, true };
v0[valarray<bool>(vb, 6)] = v1;
// v0 == valarray<char>("abABeCghijklmnop", 16)
```

— *end example*]

```cpp
valarray operator[](const valarray<size_t>& indarr) const;
```

**8** *Returns:* A `valarray` containing those elements of the controlled sequence designated by `indarr`. [*Example 7*:

```cpp
const valarray<char> v0("abcdefghijklmnop", 16);
const size_t vi[] = { 7, 5, 2, 3, 8 };
// v0[valarray<size_t>(vi, 5)] returns
// valarray<char>("hfcdi", 5)
```

— *end example*]

```cpp
indirect_array<T> operator[](const valarray<size_t>& indarr);
```

**9** *Returns:* An object that holds references to elements of the controlled sequence selected by `indarr`. [*Example 8*:

```cpp
valarray<char> v0("abcdefghijklmnop", 16);
valarray<char> v1("ABCDE", 5);
const size_t vi[] = { 7, 5, 2, 3, 8 };
v0[valarray<size_t>(vi, 5)] = v1;
// v0 == valarray<char>("abCDeBgAEjklmnop", 16)
```

— *end example*]
