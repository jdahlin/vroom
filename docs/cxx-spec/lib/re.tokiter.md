# 30.12.2 Class template `regex_token_iterator` [re.tokiter]

[Contents](../README.md) · up: [30.12 Regular expression iterators \[re.iter\]](re.iter.md) · prev: [30.12.1.4 Increment \[re.regiter.incr\]](re.regiter.incr.md) · next: [30.12.2.1 Constructors \[re.tokiter.cnstr\]](re.tokiter.cnstr.md)

**1** The class template `regex_token_iterator` is an iterator adaptor; that is to say it represents a new view of an existing iterator sequence, by enumerating all the occurrences of a regular expression within that sequence, and presenting one or more sub-expressions for each match found. Each position enumerated by the iterator is a `sub_match` class template instance that represents what matched a particular sub-expression within the regular expression.

**2** When class `regex_token_iterator` is used to enumerate a single sub-expression with index -1 the iterator performs field splitting: that is to say it enumerates one sub-expression for each section of the character container sequence that does not match the regular expression specified.

**3** After it is constructed, the iterator finds and stores a value `regex_iterator<BidirectionalIterator> position` and sets the internal count `N` to zero. It also maintains a sequence `subs` which contains a list of the sub-expressions which will be enumerated. Every time `operator++` is used the count `N` is incremented; if `N` exceeds or equals `subs.size()`, then the iterator increments member `position` and sets count `N` to zero.

**4** If the end of sequence is reached (`position` is equal to the end of sequence iterator), the iterator becomes equal to the end-of-sequence iterator value, unless the sub-expression being enumerated has index -1, in which case the iterator enumerates one last sub-expression that contains all the characters from the end of the last regular expression match to the end of the input sequence being enumerated, provided that this would not be an empty sub-expression.

**5** The default constructor constructs an end-of-sequence iterator object, which is the only legitimate iterator to be used for the end condition. The result of `operator*` on an end-of-sequence iterator is not defined. For any other iterator value a `const sub_match<BidirectionalIterator>&` is returned. The result of `operator->` on an end-of-sequence iterator is not defined. For any other iterator value a `const sub_match<BidirectionalIterator>*` is returned.

**6** It is impossible to store things into `regex_token_iterator`s. Two end-of-sequence iterators are always equal. An end-of-sequence iterator is not equal to a non-end-of-sequence iterator. Two non-end-of-sequence iterators are equal when they are constructed from the same arguments.

```cpp
namespace std {
  template<class BidirectionalIterator,
            class charT = typename iterator_traits<BidirectionalIterator>::value_type,
            class traits = regex_traits<charT>>
    class regex_token_iterator {
    public:
      using regex_type        = basic_regex<charT, traits>;
      using iterator_category = forward_iterator_tag;
      using value_type        = sub_match<BidirectionalIterator>;
      using difference_type   = ptrdiff_t;
      using pointer           = const value_type*;
      using reference         = const value_type&;

      regex_token_iterator();
      regex_token_iterator(BidirectionalIterator a, BidirectionalIterator b,
                           const regex_type& re,
                           int submatch = 0,
                           regex_constants::match_flag_type m =
                             regex_constants::match_default);
      regex_token_iterator(BidirectionalIterator a, BidirectionalIterator b,
                           const regex_type& re,
                           const vector<int>& submatches,
                           regex_constants::match_flag_type m =
                             regex_constants::match_default);
      regex_token_iterator(BidirectionalIterator a, BidirectionalIterator b,
                           const regex_type& re,
                           initializer_list<int> submatches,
                           regex_constants::match_flag_type m =
                             regex_constants::match_default);
      template<size_t N>
        regex_token_iterator(BidirectionalIterator a, BidirectionalIterator b,
                             const regex_type& re,
                             const int (&submatches)[N],
                             regex_constants::match_flag_type m =
                               regex_constants::match_default);
      regex_token_iterator(BidirectionalIterator a, BidirectionalIterator b,
                           const regex_type&& re,
                           int submatch = 0,
                           regex_constants::match_flag_type m =
                             regex_constants::match_default) = delete;
      regex_token_iterator(BidirectionalIterator a, BidirectionalIterator b,
                           const regex_type&& re,
                           const vector<int>& submatches,
                           regex_constants::match_flag_type m =
                             regex_constants::match_default) = delete;
      regex_token_iterator(BidirectionalIterator a, BidirectionalIterator b,
                           const regex_type&& re,
                           initializer_list<int> submatches,
                           regex_constants::match_flag_type m =
                             regex_constants::match_default) = delete;
      template<size_t N>
      regex_token_iterator(BidirectionalIterator a, BidirectionalIterator b,
                           const regex_type&& re,
                           const int (&submatches)[N],
                           regex_constants::match_flag_type m =
                             regex_constants::match_default) = delete;
      regex_token_iterator(const regex_token_iterator&);
      regex_token_iterator& operator=(const regex_token_iterator&);
      bool operator==(const regex_token_iterator&) const;
      const value_type& operator*() const;
      const value_type* operator->() const;
      regex_token_iterator& operator++();
      regex_token_iterator operator++(int);

    private:
      using position_iterator =
        regex_iterator<BidirectionalIterator, charT, traits>;   // exposition only
      position_iterator position;                               // exposition only
      const value_type* result;                                 // exposition only
      value_type suffix;                                        // exposition only
      size_t N;                                                 // exposition only
      vector<int> subs;                                         // exposition only
    };
}
```

**7** A *suffix iterator* is a `regex_token_iterator` object that points to a final sequence of characters at the end of the target sequence. In a suffix iterator the member `result` holds a pointer to the data member `suffix`, the value of the member `suffix.match` is `true`, `suffix.first` points to the beginning of the final sequence, and `suffix.second` points to the end of the final sequence.

**8** [*Note 1*: For a suffix iterator, data member `suffix.first` is the same as the end of the last match found, and `suffix.second` is the same as the end of the target sequence. — *end note*]

**9** The *current match* is `(*position).prefix()` if `subs[N] == -1`, or `(*position)[subs[N]]` for any other value of `subs[N]`.

## Subclauses

- [30.12.2.1 Constructors \[re.tokiter.cnstr\]](re.tokiter.cnstr.md)
- [30.12.2.2 Comparisons \[re.tokiter.comp\]](re.tokiter.comp.md)
- [30.12.2.3 Indirection \[re.tokiter.deref\]](re.tokiter.deref.md)
- [30.12.2.4 Increment \[re.tokiter.incr\]](re.tokiter.incr.md)
