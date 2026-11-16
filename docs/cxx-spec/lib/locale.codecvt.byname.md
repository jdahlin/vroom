# 28.4.1.5 Class template `codecvt_byname` [locale.codecvt.byname]

[Contents](../README.md) · up: [28.4.1 The `ctype` category \[category.ctype\]](category.ctype.md) · prev: [28.4.1.4.2 Virtual functions \[locale.codecvt.virtuals\]](locale.codecvt.virtuals.md) · next: [28.4.2 The numeric category \[category.numeric\]](category.numeric.md)

```cpp
namespace std {
  template<class internT, class externT, class stateT>
    class codecvt_byname : public codecvt<internT, externT, stateT> {
    public:
      explicit codecvt_byname(const char*, size_t refs = 0);
      explicit codecvt_byname(const string&, size_t refs = 0);

    protected:
      ~codecvt_byname();
    };
}
```
