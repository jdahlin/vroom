# 28.4.1.3 `ctype<char>` specialization [facet.ctype.special]

[Contents](../README.md) · up: [28.4.1 The `ctype` category \[category.ctype\]](category.ctype.md) · prev: [28.4.1.2 Class template `ctype_byname` \[locale.ctype.byname\]](locale.ctype.byname.md) · next: [28.4.1.3.1 Destructor \[facet.ctype.char.dtor\]](facet.ctype.char.dtor.md)

```cpp
namespace std {
  template<>
    class ctype<char> : public locale::facet, public ctype_base {
    public:
      using char_type = char;

      explicit ctype(const mask* tab = nullptr, bool del = false, size_t refs = 0);

      bool is(mask m, char c) const;
      const char* is(const char* low, const char* high, mask* vec) const;
      const char* scan_is (mask m, const char* low, const char* high) const;
      const char* scan_not(mask m, const char* low, const char* high) const;

      char        toupper(char c) const;
      const char* toupper(char* low, const char* high) const;
      char        tolower(char c) const;
      const char* tolower(char* low, const char* high) const;

      char  widen(char c) const;
      const char* widen(const char* low, const char* high, char* to) const;
      char  narrow(char c, char dfault) const;
      const char* narrow(const char* low, const char* high, char dfault, char* to) const;

      static locale::id id;
      static const size_t table_size = implementation-defined;

      const mask* table() const noexcept;
      static const mask* classic_table() noexcept;

    protected:
      ~ctype();
      virtual char        do_toupper(char c) const;
      virtual const char* do_toupper(char* low, const char* high) const;
      virtual char        do_tolower(char c) const;
      virtual const char* do_tolower(char* low, const char* high) const;

      virtual char        do_widen(char c) const;
      virtual const char* do_widen(const char* low, const char* high, char* to) const;
      virtual char        do_narrow(char c, char dfault) const;
      virtual const char* do_narrow(const char* low, const char* high,
                                    char dfault, char* to) const;
    };
}
```

**1** A specialization `ctype<char>` is provided so that the member functions on type `char` can be implemented inline.[^1] The implementation-defined value of member `table_size` is at least 256.

## Subclauses

- [28.4.1.3.1 Destructor \[facet.ctype.char.dtor\]](facet.ctype.char.dtor.md)
- [28.4.1.3.2 Members \[facet.ctype.char.members\]](facet.ctype.char.members.md)
- [28.4.1.3.3 Static members \[facet.ctype.char.statics\]](facet.ctype.char.statics.md)
- [28.4.1.3.4 Virtual functions \[facet.ctype.char.virtuals\]](facet.ctype.char.virtuals.md)

[^1]: Only the `char` (not `unsigned char` and `signed char`) form is provided. The specialization is specified in the standard, and not left as an implementation detail, because it affects the derivation interface for `ctype<char>`.
