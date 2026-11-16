# 28.4.3.2 Class template `numpunct_byname` [locale.numpunct.byname]

[Contents](../README.md) · up: [28.4.3 The numeric punctuation facet \[facet.numpunct\]](facet.numpunct.md) · prev: [28.4.3.1.2 Virtual functions \[facet.numpunct.virtuals\]](facet.numpunct.virtuals.md) · next: [28.4.4 The collate category \[category.collate\]](category.collate.md)

```cpp
namespace std {
  template<class charT>
    class numpunct_byname : public numpunct<charT> {
    // this class is specialized for char and wchar_t.
    public:
      using char_type   = charT;
      using string_type = basic_string<charT>;

      explicit numpunct_byname(const char*, size_t refs = 0);
      explicit numpunct_byname(const string&, size_t refs = 0);

    protected:
      ~numpunct_byname();
    };
}
```
