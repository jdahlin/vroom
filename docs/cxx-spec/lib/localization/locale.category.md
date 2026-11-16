# 28.3.1.1.1 Type `locale::category` [locale.category]

[Contents](../../README.md) · up: [28.3.1.1 Types \[locale.types\]](locale.types.md) · prev: [28.3.1.1 Types \[locale.types\]](locale.types.md) · next: [28.3.1.1.2 Class `locale::facet` \[locale.facet\]](locale.facet.md)

```cpp
using category = int;
```

**1** *Valid* `category` values include the `locale` member bitmask elements `collate`, `ctype`, `monetary`, `numeric`, `time`, and `messages`, each of which represents a single locale category. In addition, `locale` member bitmask constant `none` is defined as zero and represents no category. And `locale` member bitmask constant `all` is defined such that the expression

```cpp
(collate | ctype | monetary | numeric | time | messages | all) == all
```

is `true`, and represents the union of all categories. Further, the expression `(X | Y)`, where `X` and `Y` each represent a single category, represents the union of the two categories.

**2** `locale` member functions expecting a `category` argument require one of the `category` values defined above, or the union of two or more such values. Such a `category` value identifies a set of locale categories. Each locale category, in turn, identifies a set of locale facets, including at least those shown in [Table 102](locale.category.md#tab:locale.category.facets).

<a id="tab:locale.category.facets"></a>
**Table 102: Locale category facets** [tab:locale.category.facets]

| Category | Includes facets |
| --- | --- |
| collate | `collate<char>`, `collate<wchar_t>` |
| ctype | `ctype<char>`, `ctype<wchar_t>` |
|  | `codecvt<char, char, mbstate_t>` |
|  | `codecvt<char16_t, char8_t, mbstate_t>` |
|  | `codecvt<char32_t, char8_t, mbstate_t>` |
|  | `codecvt<wchar_t, char, mbstate_t>` |
| monetary | `moneypunct<char>`, `moneypunct<wchar_t>` |
|  | `moneypunct<char, true>`, `moneypunct<wchar_t, true>` |
|  | `money_get<char>`, `money_get<wchar_t>` |
|  | `money_put<char>`, `money_put<wchar_t>` |
| numeric | `numpunct<char>`, `numpunct<wchar_t>` |
|  | `num_get<char>`, `num_get<wchar_t>` |
|  | `num_put<char>`, `num_put<wchar_t>` |
| time | `time_get<char>`, `time_get<wchar_t>` |
|  | `time_put<char>`, `time_put<wchar_t>` |
| messages | `messages<char>`, `messages<wchar_t>` |

**3** For any locale `loc` either constructed, or returned by `locale::classic()`, and any facet `Facet` shown in [Table 102](locale.category.md#tab:locale.category.facets), `has_facet<Facet>(loc)` is `true`. Each `locale` member function which takes a `locale::category` argument operates on the corresponding set of facets.

**4** An implementation is required to provide those specializations for facet templates identified as members of a category, and for those shown in [Table 103](locale.category.md#tab:locale.spec).

<a id="tab:locale.spec"></a>
**Table 103: Required specializations** [tab:locale.spec]

| Category | Includes facets |
| --- | --- |
| collate | `collate_byname<char>`, `collate_byname<wchar_t>` |
| ctype | `ctype_byname<char>`, `ctype_byname<wchar_t>` |
|  | `codecvt_byname<char, char, mbstate_t>` |
|  | `codecvt_byname<char16_t, char8_t, mbstate_t>` |
|  | `codecvt_byname<char32_t, char8_t, mbstate_t>` |
|  | `codecvt_byname<wchar_t, char, mbstate_t>` |
| monetary | `moneypunct_byname<char, International>` |
|  | `moneypunct_byname<wchar_t, International>` |
|  | `money_get<C, InputIterator>` |
|  | `money_put<C, OutputIterator>` |
| numeric | `numpunct_byname<char>`, `numpunct_byname<wchar_t>` |
|  | `num_get<C, InputIterator>`, `num_put<C, OutputIterator>` |
| time | `time_get<char, InputIterator>` |
|  | `time_get_byname<char, InputIterator>` |
|  | `time_get<wchar_t, InputIterator>` |
|  | `time_get_byname<wchar_t, InputIterator>` |
|  | `time_put<char, OutputIterator>` |
|  | `time_put_byname<char, OutputIterator>` |
|  | `time_put<wchar_t, OutputIterator>` |
|  | `time_put_byname<wchar_t, OutputIterator>` |
| messages | `messages_byname<char>`, `messages_byname<wchar_t>` |

**5** The provided implementation of members of facets `num_get<charT>` and `num_put<charT>` calls `use_facet<F>(l)` only for facet `F` of types `numpunct<charT>` and `ctype<charT>`, and for locale `l` the value obtained by calling member `getloc()` on the `ios_base&` argument to these functions.

**6** In declarations of facets, a template parameter with name `InputIterator` or `OutputIterator` indicates the set of all possible specializations on parameters that meet the *Cpp17InputIterator* requirements or *Cpp17OutputIterator* requirements, respectively ([[iterator.requirements]](../iterators/iterator.requirements.md)). A template parameter with name `C` represents the set of types containing `char`, `wchar_t`, and any other implementation-defined character types that meet the requirements for a character on which any of the iostream components can be instantiated. A template parameter with name `International` represents the set of all possible specializations on a bool parameter.
