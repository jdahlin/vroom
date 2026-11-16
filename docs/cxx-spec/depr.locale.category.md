# D.22 Deprecated locale category facets [depr.locale.category]

[Contents](README.md) · up: [Annex D Compatibility features \[depr\]](depr.md) · prev: [D.21.2 Class template `wbuffer_convert` \[depr.conversions.buffer\]](depr.conversions.buffer.md) · next: [D.23 Deprecated filesystem path factory functions \[depr.fs.path.factory\]](depr.fs.path.factory.md)

**1** The `ctype` locale category includes the following facets as if they were specified in table [Table 102](lib/locale.category.md#tab:locale.category.facets) of [[locale.category]](lib/locale.category.md).

```cpp
codecvt<char16_t, char, mbstate_t>
codecvt<char32_t, char, mbstate_t>
```

**2** The `ctype` locale category includes the following facets as if they were specified in table [Table 103](lib/locale.category.md#tab:locale.spec) of [[locale.category]](lib/locale.category.md).

```cpp
codecvt_byname<char16_t, char, mbstate_t>
codecvt_byname<char32_t, char, mbstate_t>
```

**3** The following class template specializations are required in addition to those specified in [[locale.codecvt]](lib/locale.codecvt.md). The specialization `codecvt<char16_t, char, mbstate_t>` converts between the UTF-16 and UTF-8 encoding forms, and the specialization `codecvt<char32_t, char, mbstate_t>` converts between the UTF-32 and UTF-8 encoding forms.
