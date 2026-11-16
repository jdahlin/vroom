# 23.5.4.5 Navigation [common.iter.nav]

[Contents](../README.md) · up: [23.5.4 Common iterators \[iterators.common\]](iterators.common.md) · prev: [23.5.4.4 Accessors \[common.iter.access\]](common.iter.access.md) · next: [23.5.4.6 Comparisons \[common.iter.cmp\]](common.iter.cmp.md)

```cpp
common_iterator& operator++();
```

**1** *Preconditions:* `holds_alternative<I>(v_)`.

**2** *Effects:* Equivalent to `++get<I>(v_)`.

**3** *Returns:* `*this`.

```cpp
decltype(auto) operator++(int);
```

**4** *Preconditions:* `holds_alternative<I>(v_)`.

**5** *Effects:* If `I` models `forward_iterator`, equivalent to:

```cpp
common_iterator tmp = *this;
++*this;
return tmp;
```

Otherwise, equivalent to: `return get<I>(v_)++;`
