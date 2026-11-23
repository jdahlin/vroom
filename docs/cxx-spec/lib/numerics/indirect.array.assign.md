# 26.7.9.2 Assignment [indirect.array.assign]

[Contents](../../README.md) · up: [26.7.9 Class template `indirect_array` \[template.indirect.array\]](template.indirect.array.md) · prev: [26.7.9.1 Overview \[template.indirect.array.overview\]](template.indirect.array.overview.md) · next: [26.7.9.3 Compound assignment \[indirect.array.comp.assign\]](indirect.array.comp.assign.md)

```cpp
void operator=(const valarray<T>&) const;
const indirect_array& operator=(const indirect_array&) const;
```

**1** These assignment operators have reference semantics, assigning the values of the argument array elements to selected elements of the `valarray<T>` object to which it refers.

**2** If the `indirect_array` specifies an element in the `valarray<T>` object to which it refers more than once, the behavior is undefined.

**3** [*Example 1*:

```cpp
int addr[] = {2, 3, 1, 4, 4};
valarray<size_t> indirect(addr, 5);
valarray<double> a(0., 10), b(1., 5);
a[indirect] = b;
```

results in undefined behavior since element 4 is specified twice in the indirection. — *end example*]
