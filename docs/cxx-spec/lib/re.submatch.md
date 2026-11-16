# 30.9 Class template `sub_match` [re.submatch]

[Contents](../README.md) · up: [30 Regular expressions library \[re\]](re.md) · prev: [30.8.6 Non-member functions \[re.regex.nonmemb\]](re.regex.nonmemb.md) · next: [30.9.1 Members \[re.submatch.members\]](re.submatch.members.md)

**1** Class template `sub_match` denotes the sequence of characters matched by a particular marked sub-expression.

```cpp
namespace std {
  template<class BidirectionalIterator>
    class sub_match : public pair<BidirectionalIterator, BidirectionalIterator> {
    public:
      using value_type      =
              typename iterator_traits<BidirectionalIterator>::value_type;
      using difference_type =
              typename iterator_traits<BidirectionalIterator>::difference_type;
      using iterator        = BidirectionalIterator;
      using string_type     = basic_string<value_type>;

      bool matched;

      constexpr sub_match();

      difference_type length() const;
      operator string_type() const;
      string_type str() const;

      int compare(const sub_match& s) const;
      int compare(const string_type& s) const;
      int compare(const value_type* s) const;
    };
}
```

## Subclauses

- [30.9.1 Members \[re.submatch.members\]](re.submatch.members.md)
- [30.9.2 Non-member operators \[re.submatch.op\]](re.submatch.op.md)
