# 28.3.1.3 Members [locale.members]

[Contents](../../README.md) · up: [28.3.1 Class `locale` \[locale\]](locale.md) · prev: [28.3.1.2 Constructors and destructor \[locale.cons\]](locale.cons.md) · next: [28.3.1.4 Operators \[locale.operators\]](locale.operators.md)

```cpp
template<class Facet> locale combine(const locale& other) const;
```

**1** *Effects:* Constructs a locale incorporating all facets from `*this` except for that one facet of `other` that is identified by `Facet`.

**2** *Returns:* The newly created locale.

**3** *Throws:* `runtime_error` if `has_facet<Facet>(other)` is `false`.

**4** *Remarks:* The resulting locale has no name.

```cpp
string name() const;
```

**5** *Returns:* The name of `*this`, if it has one; otherwise, the string `"*"`.
