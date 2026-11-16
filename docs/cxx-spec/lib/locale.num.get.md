# 28.4.2.1 Class template `num_get` [locale.num.get]

[Contents](../README.md) · up: [28.4.2 The numeric category \[category.numeric\]](category.numeric.md) · prev: [28.4.2 The numeric category \[category.numeric\]](category.numeric.md) · next: [28.4.2.1.1 Members \[facet.num.get.members\]](facet.num.get.members.md)

```cpp
namespace std {
  template<class charT, class InputIterator = istreambuf_iterator<charT>>
    class num_get : public locale::facet {
    public:
      using char_type = charT;
      using iter_type = InputIterator;

      explicit num_get(size_t refs = 0);

      iter_type get(iter_type in, iter_type end, ios_base&,
                    ios_base::iostate& err, bool& v) const;
      iter_type get(iter_type in, iter_type end, ios_base&,
                    ios_base::iostate& err, long& v) const;
      iter_type get(iter_type in, iter_type end, ios_base&,
                    ios_base::iostate& err, long long& v) const;
      iter_type get(iter_type in, iter_type end, ios_base&,
                    ios_base::iostate& err, unsigned short& v) const;
      iter_type get(iter_type in, iter_type end, ios_base&,
                    ios_base::iostate& err, unsigned int& v) const;
      iter_type get(iter_type in, iter_type end, ios_base&,
                    ios_base::iostate& err, unsigned long& v) const;
      iter_type get(iter_type in, iter_type end, ios_base&,
                    ios_base::iostate& err, unsigned long long& v) const;
      iter_type get(iter_type in, iter_type end, ios_base&,
                    ios_base::iostate& err, float& v) const;
      iter_type get(iter_type in, iter_type end, ios_base&,
                    ios_base::iostate& err, double& v) const;
      iter_type get(iter_type in, iter_type end, ios_base&,
                    ios_base::iostate& err, long double& v) const;
      iter_type get(iter_type in, iter_type end, ios_base&,
                    ios_base::iostate& err, void*& v) const;

      static locale::id id;

    protected:
      ~num_get();
      virtual iter_type do_get(iter_type, iter_type, ios_base&,
                               ios_base::iostate& err, bool& v) const;
      virtual iter_type do_get(iter_type, iter_type, ios_base&,
                               ios_base::iostate& err, long& v) const;
      virtual iter_type do_get(iter_type, iter_type, ios_base&,
                               ios_base::iostate& err, long long& v) const;
      virtual iter_type do_get(iter_type, iter_type, ios_base&,
                               ios_base::iostate& err, unsigned short& v) const;
      virtual iter_type do_get(iter_type, iter_type, ios_base&,
                               ios_base::iostate& err, unsigned int& v) const;
      virtual iter_type do_get(iter_type, iter_type, ios_base&,
                               ios_base::iostate& err, unsigned long& v) const;
      virtual iter_type do_get(iter_type, iter_type, ios_base&,
                               ios_base::iostate& err, unsigned long long& v) const;
      virtual iter_type do_get(iter_type, iter_type, ios_base&,
                               ios_base::iostate& err, float& v) const;
      virtual iter_type do_get(iter_type, iter_type, ios_base&,
                               ios_base::iostate& err, double& v) const;
      virtual iter_type do_get(iter_type, iter_type, ios_base&,
                               ios_base::iostate& err, long double& v) const;
      virtual iter_type do_get(iter_type, iter_type, ios_base&,
                               ios_base::iostate& err, void*& v) const;
    };
}
```

**1** The facet `num_get` is used to parse numeric values from an input sequence such as an istream.

## Subclauses

- [28.4.2.1.1 Members \[facet.num.get.members\]](facet.num.get.members.md)
- [28.4.2.1.2 Virtual functions \[facet.num.get.virtuals\]](facet.num.get.virtuals.md)
