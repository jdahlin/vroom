# 28.3.3.2.1 Character conversions [conversions.character]

[Contents](../README.md) · up: [28.3.3.2 Conversions \[conversions\]](conversions.md) · prev: [28.3.3.2 Conversions \[conversions\]](conversions.md) · next: [28.4 Standard `locale` categories \[locale.categories\]](locale.categories.md)

```cpp
template<class charT> charT toupper(charT c, const locale& loc);
```

**1** *Returns:* `use_facet<ctype<charT>>(loc).toupper(c)`.

```cpp
template<class charT> charT tolower(charT c, const locale& loc);
```

**2** *Returns:* `use_facet<ctype<charT>>(loc).tolower(c)`.
