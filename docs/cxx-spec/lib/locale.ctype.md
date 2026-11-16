# 28.4.1.1 Class template `ctype` [locale.ctype]

[Contents](../README.md) · up: [28.4.1 The `ctype` category \[category.ctype\]](category.ctype.md) · prev: [28.4.1 The `ctype` category \[category.ctype\]](category.ctype.md) · next: [28.4.1.1.1 `ctype` members \[locale.ctype.members\]](locale.ctype.members.md)

```cpp
namespace std {
  template<class charT>
    class ctype : public locale::facet, public ctype_base {
    public:
      using char_type = charT;

      explicit ctype(size_t refs = 0);

      bool         is(mask m, charT c) const;
      const charT* is(const charT* low, const charT* high, mask* vec) const;
      const charT* scan_is(mask m, const charT* low, const charT* high) const;
      const charT* scan_not(mask m, const charT* low, const charT* high) const;
      charT        toupper(charT c) const;
      const charT* toupper(charT* low, const charT* high) const;
      charT        tolower(charT c) const;
      const charT* tolower(charT* low, const charT* high) const;

      charT        widen(char c) const;
      const char*  widen(const char* low, const char* high, charT* to) const;
      char         narrow(charT c, char dfault) const;
      const charT* narrow(const charT* low, const charT* high, char dfault, char* to) const;

      static locale::id id;

    protected:
      ~ctype();
      virtual bool         do_is(mask m, charT c) const;
      virtual const charT* do_is(const charT* low, const charT* high, mask* vec) const;
      virtual const charT* do_scan_is(mask m, const charT* low, const charT* high) const;
      virtual const charT* do_scan_not(mask m, const charT* low, const charT* high) const;
      virtual charT        do_toupper(charT) const;
      virtual const charT* do_toupper(charT* low, const charT* high) const;
      virtual charT        do_tolower(charT) const;
      virtual const charT* do_tolower(charT* low, const charT* high) const;
      virtual charT        do_widen(char) const;
      virtual const char*  do_widen(const char* low, const char* high, charT* dest) const;
      virtual char         do_narrow(charT, char dfault) const;
      virtual const charT* do_narrow(const charT* low, const charT* high,
                                     char dfault, char* dest) const;
    };
}
```

**1** Class `ctype` encapsulates the C library `<cctype>` features. `istream` members are required to use `ctype<>` for character classing during input parsing.

**2** The specializations required in [Table 102](locale.category.md#tab:locale.category.facets) ([[locale.category]](locale.category.md)), namely `ctype<char>` and `ctype<wchar_t>`, implement character classing appropriate to the implementation’s native character set.

## Subclauses

- [28.4.1.1.1 `ctype` members \[locale.ctype.members\]](locale.ctype.members.md)
- [28.4.1.1.2 `ctype` virtual functions \[locale.ctype.virtuals\]](locale.ctype.virtuals.md)
