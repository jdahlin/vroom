# 28.3.3.1 Character classification [classification]

[Contents](../README.md) · up: [28.3.3 Convenience interfaces \[locale.convenience\]](locale.convenience.md) · prev: [28.3.3 Convenience interfaces \[locale.convenience\]](locale.convenience.md) · next: [28.3.3.2 Conversions \[conversions\]](conversions.md)

```cpp
template<class charT> bool isspace (charT c, const locale& loc);
template<class charT> bool isprint (charT c, const locale& loc);
template<class charT> bool iscntrl (charT c, const locale& loc);
template<class charT> bool isupper (charT c, const locale& loc);
template<class charT> bool islower (charT c, const locale& loc);
template<class charT> bool isalpha (charT c, const locale& loc);
template<class charT> bool isdigit (charT c, const locale& loc);
template<class charT> bool ispunct (charT c, const locale& loc);
template<class charT> bool isxdigit(charT c, const locale& loc);
template<class charT> bool isalnum (charT c, const locale& loc);
template<class charT> bool isgraph (charT c, const locale& loc);
template<class charT> bool isblank (charT c, const locale& loc);
```

**1** Each of these functions `isF` returns the result of the expression:

```cpp
use_facet<ctype<charT>>(loc).is(ctype_base::F, c)
```

where `F` is the `ctype_base::mask` value corresponding to that function ([[category.ctype]](category.ctype.md)).[^1]

[^1]: When used in a loop, it is faster to cache the `ctype<>` facet and use it directly, or use the vector form of `ctype<>::is`.
