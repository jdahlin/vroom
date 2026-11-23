# 28.4.6.4 Class template `moneypunct_byname` [locale.moneypunct.byname]

[Contents](../../README.md) · up: [28.4.6 The monetary category \[category.monetary\]](category.monetary.md) · prev: [28.4.6.3.2 Virtual functions \[locale.moneypunct.virtuals\]](locale.moneypunct.virtuals.md) · next: [28.4.7 The message retrieval category \[category.messages\]](category.messages.md)

```cpp
namespace std {
  template<class charT, bool Intl = false>
  class moneypunct_byname : public moneypunct<charT, Intl> {
    public:
      using pattern     = money_base::pattern;
      using string_type = basic_string<charT>;

      explicit moneypunct_byname(const char*, size_t refs = 0);
      explicit moneypunct_byname(const string&, size_t refs = 0);

    protected:
      ~moneypunct_byname();
    };
}
```
