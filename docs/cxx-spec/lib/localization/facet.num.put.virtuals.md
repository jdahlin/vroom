# 28.4.2.2.2 Virtual functions [facet.num.put.virtuals]

[Contents](../../README.md) · up: [28.4.2.2 Class template `num_put` \[locale.nm.put\]](locale.nm.put.md) · prev: [28.4.2.2.1 Members \[facet.num.put.members\]](facet.num.put.members.md) · next: [28.4.3 The numeric punctuation facet \[facet.numpunct\]](facet.numpunct.md)

```cpp
iter_type do_put(iter_type out, ios_base& str, char_type fill, long val) const;
iter_type do_put(iter_type out, ios_base& str, char_type fill, long long val) const;
iter_type do_put(iter_type out, ios_base& str, char_type fill, unsigned long val) const;
iter_type do_put(iter_type out, ios_base& str, char_type fill, unsigned long long val) const;
iter_type do_put(iter_type out, ios_base& str, char_type fill, double val) const;
iter_type do_put(iter_type out, ios_base& str, char_type fill, long double val) const;
iter_type do_put(iter_type out, ios_base& str, char_type fill, const void* val) const;
```

**1** *Effects:* Writes characters to the sequence `out`, formatting `val` as desired. In the following description, `loc` names a local variable initialized as

```cpp
locale loc = str.getloc();
```

**2** The details of this operation occur in several stages:

- Stage 1: Determine a printf conversion specifier `spec` and determine the characters that would be printed by `printf` ([[c.files]](../io/c.files.md)) given this conversion specifier for

  ```cpp
  printf(spec, val)
  ```

  assuming that the current locale is the `"C"` locale.

- Stage 2: Adjust the representation by converting each `char` determined by stage 1 to a `charT` using a conversion and values returned by members of `use_facet<numpunct<charT>>(loc)`.

- Stage 3: Determine where padding is required.

- Stage 4: Insert the sequence into the `out`.

**3** Detailed descriptions of each stage follow.

**4** *Returns:* `out`.

**5**

- **Stage 1:** The first action of stage 1 is to determine a conversion specifier. The tables that describe this determination use the following local variables

  ```cpp
  fmtflags flags = str.flags();
  fmtflags basefield =  (flags & (ios_base::basefield));
  fmtflags uppercase =  (flags & (ios_base::uppercase));
  fmtflags floatfield = (flags & (ios_base::floatfield));
  fmtflags showpos =    (flags & (ios_base::showpos));
  fmtflags showbase =   (flags & (ios_base::showbase));
  fmtflags showpoint =  (flags & (ios_base::showpoint));
  ```

  All tables used in describing stage 1 are ordered. That is, the first line whose condition is true applies. A line without a condition is the default behavior when none of the earlier lines apply.

  For conversion from an integral type other than a character type, the function determines the integral conversion specifier as indicated in [Table 108](facet.num.put.virtuals.md#tab:facet.num.put.int).

  <a id="tab:facet.num.put.int"></a>
  **Table 108: Integer conversions** [tab:facet.num.put.int]

  | State | `stdio` equivalent |
  | --- | --- |
  | `basefield == ios_base::oct` | `%o` |
  | `(basefield == ios_base::hex) && !uppercase` | `%x` |
  | `(basefield == ios_base::hex)` | `%X` |
  | for a `signed` integral type | `%d` |
  | for an `unsigned` integral type | `%u` |

  For conversion from a floating-point type, the function determines the floating-point conversion specifier as indicated in [Table 109](facet.num.put.virtuals.md#tab:facet.num.put.fp).

  <a id="tab:facet.num.put.fp"></a>
  **Table 109: Floating-point conversions** [tab:facet.num.put.fp]

  | State | `stdio` equivalent |
  | --- | --- |
  | `floatfield == ios_base::fixed` | `%f` |
  | `floatfield == ios_base::scientific && !uppercase` | `%e` |
  | `floatfield == ios_base::scientific` | `%E` |
  | `floatfield == (ios_base::fixed \| ios_base::scientific) && !uppercase` | `%a` |
  | `floatfield == (ios_base::fixed \| ios_base::scientific)` | `%A` |
  | `!uppercase` | `%g` |
  | *otherwise* | `%G` |

  For conversions from an integral or floating-point type a length modifier is added to the conversion specifier as indicated in [Table 110](facet.num.put.virtuals.md#tab:facet.num.put.length).

  <a id="tab:facet.num.put.length"></a>
  **Table 110: Length modifier** [tab:facet.num.put.length]

  | Type | Length modifier |
  | --- | --- |
  | `long` | `l` |
  | `long long` | `ll` |
  | `unsigned long` | `l` |
  | `unsigned long long` | `ll` |
  | `long double` | `L` |
  | *otherwise* | *none* |

  The conversion specifier has the following optional additional qualifiers prepended as indicated in [Table 111](facet.num.put.virtuals.md#tab:facet.num.put.conv).

  <a id="tab:facet.num.put.conv"></a>
  **Table 111: Numeric conversions** [tab:facet.num.put.conv]

  | Type(s) | State | `stdio` equivalent |
  | --- | --- | --- |
  | an integral type | `showpos` | `+` |
  |  | `showbase` | `#` |
  | a floating-point type | `showpos` | `+` |
  |  | `showpoint` | `#` |

  For conversion from a floating-point type, if `floatfield != (ios_base::fixed | ios_base::scientific)`, `str.precision()` is specified as precision in the conversion specification. Otherwise, no precision is specified.

  For conversion from `void*` the specifier is `%p`.

  The representations at the end of stage 1 consists of the `char`’s that would be printed by a call of `printf(s, val)` where `s` is the conversion specifier determined above.

- **Stage 2:** Any character `c` other than a decimal point(.) is converted to a `charT` via

  ```cpp
  use_facet<ctype<charT>>(loc).widen(c)
  ```

  A local variable `punct` is initialized via

  ```cpp
  const numpunct<charT>& punct = use_facet<numpunct<charT>>(loc);
  ```

  For arithmetic types, `punct.thousands_sep()` characters are inserted into the sequence as determined by the value returned by `punct.do_grouping()` using the method described in [[facet.numpunct.virtuals]](facet.numpunct.virtuals.md)

  Decimal point characters(.) are replaced by `punct.decimal_point()`

- **Stage 3:** A local variable is initialized as

  ```cpp
  fmtflags adjustfield = (flags & (ios_base::adjustfield));
  ```

  The location of any padding[^1] is determined according to [Table 112](facet.num.put.virtuals.md#tab:facet.num.put.fill).

  <a id="tab:facet.num.put.fill"></a>
  **Table 112: Fill padding** [tab:facet.num.put.fill]

  | State | Location |
  | --- | --- |
  | `adjustfield == ios_base::left` | pad after |
  | `adjustfield == ios_base::right` | pad before |
  | `adjustfield == internal` and a sign occurs in the representation | pad after the sign |
  | `adjustfield == internal` and representation after stage 1 began with 0x or 0X | pad after x or X |
  | *otherwise* | pad before |

  If `str.width()` is nonzero and the number of `charT`’s in the sequence after stage 2 is less than `str.width()`, then enough `fill` characters are added to the sequence at the position indicated for padding to bring the length of the sequence to `str.width()`.

  `str.width(0)` is called.

- **Stage 4:** The sequence of `charT`’s at the end of stage 3 are output via

  ```cpp
  *out++ = c
  ```

```cpp
iter_type do_put(iter_type out, ios_base& str, char_type fill, bool val) const;
```

**6** *Returns:* If `(str.flags() & ios_base::boolalpha) == 0` returns `do_put(out, str, fill, (int)val)`, otherwise obtains a string `s` as if by

```cpp
string_type s =
  val ? use_facet<numpunct<charT>>(loc).truename()
      : use_facet<numpunct<charT>>(loc).falsename();
```

and then inserts each character `c` of `s` into `out` via `*out++ = c` and returns `out`.

[^1]: The conversion specification `#o` generates a leading `0` which is *not* a padding character.
