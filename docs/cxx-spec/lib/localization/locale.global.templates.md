# 28.3.2 `locale` globals [locale.global.templates]

[Contents](../../README.md) · up: [28.3 Locales \[locales\]](locales.md) · prev: [28.3.1.5 Static members \[locale.statics\]](locale.statics.md) · next: [28.3.3 Convenience interfaces \[locale.convenience\]](locale.convenience.md)

```cpp
template<class Facet> const Facet& use_facet(const locale& loc);
```

**1** *Mandates:* `Facet` is a facet class whose definition contains the public static member `id` as defined in [[locale.facet]](locale.facet.md).

**2** *Returns:* A reference to the corresponding facet of `loc`, if present.

**3** *Throws:* `bad_cast` if `has_facet<Facet>(loc)` is `false`.

**4** *Remarks:* The reference returned remains valid at least as long as any copy of `loc` exists.

```cpp
template<class Facet> bool has_facet(const locale& loc) noexcept;
```

**5** *Returns:* `true` if the facet requested is present in `loc`; otherwise `false`.
