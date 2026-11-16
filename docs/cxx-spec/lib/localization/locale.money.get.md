# 28.4.6.1 Class template `money_get` [locale.money.get]

[Contents](../../README.md) · up: [28.4.6 The monetary category \[category.monetary\]](category.monetary.md) · prev: [28.4.6 The monetary category \[category.monetary\]](category.monetary.md) · next: [28.4.6.1.1 Members \[locale.money.get.members\]](locale.money.get.members.md)

```cpp
namespace std {
  template<class charT, class InputIterator = istreambuf_iterator<charT>>
    class money_get : public locale::facet {
    public:
      using char_type   = charT;
      using iter_type   = InputIterator;
      using string_type = basic_string<charT>;

      explicit money_get(size_t refs = 0);

      iter_type get(iter_type s, iter_type end, bool intl,
                    ios_base& f, ios_base::iostate& err,
                    long double& units) const;
      iter_type get(iter_type s, iter_type end, bool intl,
                    ios_base& f, ios_base::iostate& err,
                    string_type& digits) const;

      static locale::id id;

    protected:
      ~money_get();
      virtual iter_type do_get(iter_type, iter_type, bool, ios_base&,
                               ios_base::iostate& err, long double& units) const;
      virtual iter_type do_get(iter_type, iter_type, bool, ios_base&,
                               ios_base::iostate& err, string_type& digits) const;
    };
}
```

## Subclauses

- [28.4.6.1.1 Members \[locale.money.get.members\]](locale.money.get.members.md)
- [28.4.6.1.2 Virtual functions \[locale.money.get.virtuals\]](locale.money.get.virtuals.md)
