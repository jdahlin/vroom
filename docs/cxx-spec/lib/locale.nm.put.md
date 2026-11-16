# 28.4.2.2 Class template `num_put` [locale.nm.put]

[Contents](../README.md) · up: [28.4.2 The numeric category \[category.numeric\]](category.numeric.md) · prev: [28.4.2.1.2 Virtual functions \[facet.num.get.virtuals\]](facet.num.get.virtuals.md) · next: [28.4.2.2.1 Members \[facet.num.put.members\]](facet.num.put.members.md)

```cpp
namespace std {
  template<class charT, class OutputIterator = ostreambuf_iterator<charT>>
    class num_put : public locale::facet {
    public:
      using char_type = charT;
      using iter_type = OutputIterator;

      explicit num_put(size_t refs = 0);

      iter_type put(iter_type s, ios_base& f, char_type fill, bool v) const;
      iter_type put(iter_type s, ios_base& f, char_type fill, long v) const;
      iter_type put(iter_type s, ios_base& f, char_type fill, long long v) const;
      iter_type put(iter_type s, ios_base& f, char_type fill, unsigned long v) const;
      iter_type put(iter_type s, ios_base& f, char_type fill, unsigned long long v) const;
      iter_type put(iter_type s, ios_base& f, char_type fill, double v) const;
      iter_type put(iter_type s, ios_base& f, char_type fill, long double v) const;
      iter_type put(iter_type s, ios_base& f, char_type fill, const void* v) const;

      static locale::id id;

    protected:
      ~num_put();
      virtual iter_type do_put(iter_type, ios_base&, char_type fill, bool v) const;
      virtual iter_type do_put(iter_type, ios_base&, char_type fill, long v) const;
      virtual iter_type do_put(iter_type, ios_base&, char_type fill, long long v) const;
      virtual iter_type do_put(iter_type, ios_base&, char_type fill, unsigned long) const;
      virtual iter_type do_put(iter_type, ios_base&, char_type fill, unsigned long long) const;
      virtual iter_type do_put(iter_type, ios_base&, char_type fill, double v) const;
      virtual iter_type do_put(iter_type, ios_base&, char_type fill, long double v) const;
      virtual iter_type do_put(iter_type, ios_base&, char_type fill, const void* v) const;
    };
}
```

**1** The facet `num_put` is used to format numeric values to a character sequence such as an ostream.

## Subclauses

- [28.4.2.2.1 Members \[facet.num.put.members\]](facet.num.put.members.md)
- [28.4.2.2.2 Virtual functions \[facet.num.put.virtuals\]](facet.num.put.virtuals.md)
