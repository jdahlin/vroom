# 28.4.5.3 Class template `time_put` [locale.time.put]

[Contents](../README.md) · up: [28.4.5 The time category \[category.time\]](category.time.md) · prev: [28.4.5.2 Class template `time_get_byname` \[locale.time.get.byname\]](locale.time.get.byname.md) · next: [28.4.5.3.1 Members \[locale.time.put.members\]](locale.time.put.members.md)

```cpp
namespace std {
  template<class charT, class OutputIterator = ostreambuf_iterator<charT>>
    class time_put : public locale::facet {
    public:
      using char_type = charT;
      using iter_type = OutputIterator;

      explicit time_put(size_t refs = 0);

      // the following is implemented in terms of other member functions.
      iter_type put(iter_type s, ios_base& f, char_type fill, const tm* tmb,
                    const charT* pattern, const charT* pat_end) const;
      iter_type put(iter_type s, ios_base& f, char_type fill,
                    const tm* tmb, char format, char modifier = 0) const;

      static locale::id id;

    protected:
      ~time_put();
      virtual iter_type do_put(iter_type s, ios_base&, char_type, const tm* t,
                               char format, char modifier) const;
    };
}
```

## Subclauses

- [28.4.5.3.1 Members \[locale.time.put.members\]](locale.time.put.members.md)
- [28.4.5.3.2 Virtual functions \[locale.time.put.virtuals\]](locale.time.put.virtuals.md)
