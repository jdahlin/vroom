# D.11 Relational operators [depr.relops]

[Contents](../README.md) · up: [Annex D Compatibility features \[depr\]](depr.md) · prev: [D.10 Requires paragraph \[depr.res.on.required\]](depr.res.on.required.md) · next: [D.12 `char*` streams \[depr.str.strstreams\]](depr.str.strstreams.md)

**1** The header `<utility>` ([[utility.syn]](../lib/utilities/utility.syn.md)) has the following additions:

```cpp
namespace std::rel_ops {
  template<class T> bool operator!=(const T&, const T&);
  template<class T> bool operator> (const T&, const T&);
  template<class T> bool operator<=(const T&, const T&);
  template<class T> bool operator>=(const T&, const T&);
}
```

**2** To avoid redundant definitions of `operator!=` out of `operator==` and operators `>`, `<=`, and `>=` out of `operator<`, the library provides the following:

```cpp
template<class T> bool operator!=(const T& x, const T& y);
```

**3** *Requires:* Type `T` is *Cpp17EqualityComparable* ([Table 25](../lib/library/utility.arg.requirements.md#tab:cpp17.equalitycomparable)).

**4** *Returns:* `!(x == y)`.

```cpp
template<class T> bool operator>(const T& x, const T& y);
```

**5** *Requires:* Type `T` is *Cpp17LessThanComparable* ([Table 26](../lib/library/utility.arg.requirements.md#tab:cpp17.lessthancomparable)).

**6** *Returns:* `y < x`.

```cpp
template<class T> bool operator<=(const T& x, const T& y);
```

**7** *Requires:* Type `T` is *Cpp17LessThanComparable* ([Table 26](../lib/library/utility.arg.requirements.md#tab:cpp17.lessthancomparable)).

**8** *Returns:* `!(y < x)`.

```cpp
template<class T> bool operator>=(const T& x, const T& y);
```

**9** *Requires:* Type `T` is *Cpp17LessThanComparable* ([Table 26](../lib/library/utility.arg.requirements.md#tab:cpp17.lessthancomparable)).

**10** *Returns:* `!(x < y)`.
