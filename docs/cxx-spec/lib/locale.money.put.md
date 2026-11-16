# 28.4.6.2 Class template `money_put` [locale.money.put]

[Contents](../README.md) · up: [28.4.6 The monetary category \[category.monetary\]](category.monetary.md) · prev: [28.4.6.1.2 Virtual functions \[locale.money.get.virtuals\]](locale.money.get.virtuals.md) · next: [28.4.6.2.1 Members \[locale.money.put.members\]](locale.money.put.members.md)

```cpp
namespace std {
  template<class charT, class OutputIterator = ostreambuf_iterator<charT>>
    class money_put : public locale::facet {
    public:
      using char_type   = charT;
      using iter_type   = OutputIterator;
      using string_type = basic_string<charT>;

      explicit money_put(size_t refs = 0);

      iter_type put(iter_type s, bool intl, ios_base& f,
                    char_type fill, long double units) const;
      iter_type put(iter_type s, bool intl, ios_base& f,
                    char_type fill, const string_type& digits) const;

      static locale::id id;

    protected:
      ~money_put();
      virtual iter_type do_put(iter_type, bool, ios_base&, char_type fill,
                               long double units) const;
      virtual iter_type do_put(iter_type, bool, ios_base&, char_type fill,
                               const string_type& digits) const;
    };
}
```

## Subclauses

- [28.4.6.2.1 Members \[locale.money.put.members\]](locale.money.put.members.md)
- [28.4.6.2.2 Virtual functions \[locale.money.put.virtuals\]](locale.money.put.virtuals.md)
