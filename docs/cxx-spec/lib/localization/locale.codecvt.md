# 28.4.1.4 Class template `codecvt` [locale.codecvt]

[Contents](../../README.md) · up: [28.4.1 The `ctype` category \[category.ctype\]](category.ctype.md) · prev: [28.4.1.3.4 Virtual functions \[facet.ctype.char.virtuals\]](facet.ctype.char.virtuals.md) · next: [28.4.1.4.1 Members \[locale.codecvt.members\]](locale.codecvt.members.md)

```cpp
namespace std {
  class codecvt_base {
  public:
    enum result { ok, partial, error, noconv };
  };

  template<class internT, class externT, class stateT>
    class codecvt : public locale::facet, public codecvt_base {
    public:
      using intern_type = internT;
      using extern_type = externT;
      using state_type  = stateT;

      explicit codecvt(size_t refs = 0);

      result out(
          stateT& state,
          const internT* from, const internT* from_end, const internT*& from_next,
                externT*   to,       externT*   to_end,       externT*&   to_next) const;

      result unshift(
          stateT& state,
                externT*    to,      externT*   to_end,       externT*&   to_next) const;

      result in(
          stateT& state,
          const externT* from, const externT* from_end, const externT*& from_next,
                internT*   to,       internT*   to_end,       internT*&   to_next) const;

      int encoding() const noexcept;
      bool always_noconv() const noexcept;
      int length(stateT&, const externT* from, const externT* end, size_t max) const;
      int max_length() const noexcept;

      static locale::id id;

    protected:
      ~codecvt();
      virtual result do_out(
          stateT& state,
          const internT* from, const internT* from_end, const internT*& from_next,
                externT* to,         externT*   to_end,       externT*&   to_next) const;
      virtual result do_in(
          stateT& state,
          const externT* from, const externT* from_end, const externT*& from_next,
                internT* to,         internT*   to_end,       internT*&   to_next) const;
      virtual result do_unshift(
          stateT& state,
                externT* to,         externT*   to_end,       externT*&   to_next) const;

      virtual int do_encoding() const noexcept;
      virtual bool do_always_noconv() const noexcept;
      virtual int do_length(stateT&, const externT* from, const externT* end, size_t max) const;
      virtual int do_max_length() const noexcept;
    };
}
```

**1** The class `codecvt<internT, externT, stateT>` is for use when converting from one character encoding to another, such as from wide characters to multibyte characters or between wide character encodings such as UTF-32 and EUC.

**2** The `stateT` argument selects the pair of character encodings being mapped between.

**3** The specializations required in [Table 102](locale.category.md#tab:locale.category.facets) ([[locale.category]](locale.category.md)) convert the implementation-defined native character set. `codecvt<char, char, mbstate_t>` implements a degenerate conversion; it does not convert at all. The specialization `codecvt<char16_t, char8_t, mbstate_t>` converts between the UTF-16 and UTF-8 encoding forms, and the specialization `codecvt` `<char32_t, char8_t, mbstate_t>` converts between the UTF-32 and UTF-8 encoding forms. `codecvt<wchar_t, char, mbstate_t>` converts between the native character sets for ordinary and wide characters. Specializations on `mbstate_t` perform conversion between encodings known to the library implementer. Other encodings can be converted by specializing on a program-defined `stateT` type. Objects of type `stateT` can contain any state that is useful to communicate to or from the specialized `do_in` or `do_out` members.

## Subclauses

- [28.4.1.4.1 Members \[locale.codecvt.members\]](locale.codecvt.members.md)
- [28.4.1.4.2 Virtual functions \[locale.codecvt.virtuals\]](locale.codecvt.virtuals.md)
