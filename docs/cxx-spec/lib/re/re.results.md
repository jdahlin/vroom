# 30.10 Class template `match_results` [re.results]

[Contents](../../README.md) · up: [30 Regular expressions library \[re\]](re.md) · prev: [30.9.2 Non-member operators \[re.submatch.op\]](re.submatch.op.md) · next: [30.10.1 Constructors \[re.results.const\]](re.results.const.md)

**1** Class template `match_results` denotes a collection of character sequences representing the result of a regular expression match. Storage for the collection is allocated and freed as necessary by the member functions of class template `match_results`.

**2** The class template `match_results` meets the requirements of an allocator-aware container and of a sequence container ([[container.requirements.general]](../containers/container.requirements.general.md), [[sequence.reqmts]](../containers/sequence.reqmts.md)) except that only copy assignment, move assignment, and operations defined for const-qualified sequence containers are supported and that the semantics of comparison functions are different from those required for a container.

**3** A default-constructed `match_results` object has no fully established result state. A match result is *ready* when, as a consequence of a completed regular expression match modifying such an object, its result state becomes fully established. The effects of calling most member functions from a `match_results` object that is not ready are undefined.

**4** The `sub_match` object stored at index 0 represents sub-expression 0, i.e., the whole match. In this case the `sub_match` member `matched` is always `true`. The `sub_match` object stored at index `n` denotes what matched the marked sub-expression `n` within the matched expression. If the sub-expression `n` participated in a regular expression match then the `sub_match` member `matched` evaluates to `true`, and members `first` and `second` denote the range of characters `[first, second)` which formed that match. Otherwise `matched` is `false`, and members `first` and `second` point to the end of the sequence that was searched. [*Note 1*: The `sub_match` objects representing different sub-expressions that did not participate in a regular expression match need not be distinct. — *end note*]

```cpp
namespace std {
  template<class BidirectionalIterator,
           class Allocator = allocator<sub_match<BidirectionalIterator>>>
    class match_results {
    public:
      using value_type      = sub_match<BidirectionalIterator>;
      using const_reference = const value_type&;
      using reference       = value_type&;
      using const_iterator  = implementation-defined;
      using iterator        = const_iterator;
      using difference_type =
              typename iterator_traits<BidirectionalIterator>::difference_type;
      using size_type       = typename allocator_traits<Allocator>::size_type;
      using allocator_type  = Allocator;
      using char_type       =
              typename iterator_traits<BidirectionalIterator>::value_type;
      using string_type     = basic_string<char_type>;

      // [re.results.const], construct/copy/destroy
      match_results() : match_results(Allocator()) {}
      explicit match_results(const Allocator&);
      match_results(const match_results& m);
      match_results(match_results&& m) noexcept;
      match_results& operator=(const match_results& m);
      match_results& operator=(match_results&& m);
      ~match_results();

      // [re.results.state], state
      bool ready() const;

      // [re.results.size], size
      size_type size() const;
      size_type max_size() const;
      [[nodiscard]] bool empty() const;

      // [re.results.acc], element access
      difference_type length(size_type sub = 0) const;
      difference_type position(size_type sub = 0) const;
      string_type str(size_type sub = 0) const;
      const_reference operator[](size_type n) const;

      const_reference prefix() const;
      const_reference suffix() const;
      const_iterator begin() const;
      const_iterator end() const;
      const_iterator cbegin() const;
      const_iterator cend() const;

      // [re.results.form], format
      template<class OutputIter>
        OutputIter
          format(OutputIter out,
                 const char_type* fmt_first, const char_type* fmt_last,
                 regex_constants::match_flag_type flags = regex_constants::format_default) const;
      template<class OutputIter, class ST, class SA>
        OutputIter
          format(OutputIter out,
                 const basic_string<char_type, ST, SA>& fmt,
                 regex_constants::match_flag_type flags = regex_constants::format_default) const;
      template<class ST, class SA>
        basic_string<char_type, ST, SA>
          format(const basic_string<char_type, ST, SA>& fmt,
                 regex_constants::match_flag_type flags = regex_constants::format_default) const;
      string_type
        format(const char_type* fmt,
               regex_constants::match_flag_type flags = regex_constants::format_default) const;

      // [re.results.all], allocator
      allocator_type get_allocator() const;

      // [re.results.swap], swap
      void swap(match_results& that);
    };
}
```

## Subclauses

- [30.10.1 Constructors \[re.results.const\]](re.results.const.md)
- [30.10.2 State \[re.results.state\]](re.results.state.md)
- [30.10.3 Size \[re.results.size\]](re.results.size.md)
- [30.10.4 Element access \[re.results.acc\]](re.results.acc.md)
- [30.10.5 Formatting \[re.results.form\]](re.results.form.md)
- [30.10.6 Allocator \[re.results.all\]](re.results.all.md)
- [30.10.7 Swap \[re.results.swap\]](re.results.swap.md)
- [30.10.8 Non-member functions \[re.results.nonmember\]](re.results.nonmember.md)
