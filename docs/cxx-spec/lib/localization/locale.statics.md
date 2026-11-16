# 28.3.1.5 Static members [locale.statics]

[Contents](../../README.md) · up: [28.3.1 Class `locale` \[locale\]](locale.md) · prev: [28.3.1.4 Operators \[locale.operators\]](locale.operators.md) · next: [28.3.2 `locale` globals \[locale.global.templates\]](locale.global.templates.md)

```cpp
static locale global(const locale& loc);
```

**1** *Effects:* Sets the global locale to its argument. Causes future calls to the constructor `locale()` to return a copy of the argument. If the argument has a name, does

```cpp
setlocale(LC_ALL, loc.name().c_str());
```

otherwise, the effect on the C locale, if any, is implementation-defined.

**2** *Remarks:* No library function other than `locale::global()` affects the value returned by `locale()`. [*Note 1*: See [[c.locales]](c.locales.md) for data race considerations when `setlocale` is invoked. — *end note*]

**3** *Returns:* The previous value of `locale()`.

```cpp
static const locale& classic();
```

**4** The `"C"` locale.

**5** *Returns:* A locale that implements the classic `"C"` locale semantics, equivalent to the value `locale("C")`.

**6** *Remarks:* This locale, its facets, and their member functions, do not change with time.
