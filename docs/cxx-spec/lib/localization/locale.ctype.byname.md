# 28.4.1.2 Class template `ctype_byname` [locale.ctype.byname]

[Contents](../../README.md) · up: [28.4.1 The `ctype` category \[category.ctype\]](category.ctype.md) · prev: [28.4.1.1.2 `ctype` virtual functions \[locale.ctype.virtuals\]](locale.ctype.virtuals.md) · next: [28.4.1.3 `ctype<char>` specialization \[facet.ctype.special\]](facet.ctype.special.md)

```cpp
namespace std {
  template<class charT>
    class ctype_byname : public ctype<charT> {
    public:
      using mask = typename ctype<charT>::mask;
      explicit ctype_byname(const char*, size_t refs = 0);
      explicit ctype_byname(const string&, size_t refs = 0);

    protected:
      ~ctype_byname();
    };
}
```
