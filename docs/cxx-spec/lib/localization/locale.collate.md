# 28.4.4.1 Class template `collate` [locale.collate]

[Contents](../../README.md) · up: [28.4.4 The collate category \[category.collate\]](category.collate.md) · prev: [28.4.4 The collate category \[category.collate\]](category.collate.md) · next: [28.4.4.1.1 Members \[locale.collate.members\]](locale.collate.members.md)

```cpp
namespace std {
  template<class charT>
    class collate : public locale::facet {
    public:
      using char_type   = charT;
      using string_type = basic_string<charT>;

      explicit collate(size_t refs = 0);

      int compare(const charT* low1, const charT* high1,
                  const charT* low2, const charT* high2) const;
      string_type transform(const charT* low, const charT* high) const;
      long hash(const charT* low, const charT* high) const;

      static locale::id id;

    protected:
      ~collate();
      virtual int do_compare(const charT* low1, const charT* high1,
                             const charT* low2, const charT* high2) const;
      virtual string_type do_transform(const charT* low, const charT* high) const;
      virtual long do_hash (const charT* low, const charT* high) const;
    };
}
```

**1** The class `collate<charT>` provides features for use in the collation (comparison) and hashing of strings. A locale member function template, `operator()`, uses the collate facet to allow a locale to act directly as the predicate argument for standard algorithms ([[algorithms]](../algorithms/algorithms.md)) and containers operating on strings. The specializations required in [Table 102](locale.category.md#tab:locale.category.facets) ([[locale.category]](locale.category.md)), namely `collate<char>` and `collate<wchar_t>`, apply lexicographic ordering ([[alg.lex.comparison]](../algorithms/alg.lex.comparison.md)).

**2** Each function compares a string of characters `*p` in the range `[low, high)`.

## Subclauses

- [28.4.4.1.1 Members \[locale.collate.members\]](locale.collate.members.md)
- [28.4.4.1.2 Virtual functions \[locale.collate.virtuals\]](locale.collate.virtuals.md)
