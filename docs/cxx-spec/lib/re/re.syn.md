# 30.4 Header `<regex>` synopsis [re.syn]

[Contents](../../README.md) · up: [30 Regular expressions library \[re\]](re.md) · prev: [30.3 Requirements \[re.req\]](re.req.md) · next: [30.5 Namespace `std::regex_constants` \[re.const\]](re.const.md)

```cpp
#include <compare>              // see [compare.syn]
#include <initializer_list>     // see [initializer.list.syn]

namespace std {
  // [re.const], regex constants
  namespace regex_constants {
    using syntax_option_type = T1;
    using match_flag_type = T2;
    using error_type = T3;
  }

  // [re.badexp], class regex_error
  class regex_error;

  // [re.traits], class template regex_traits
  template<class charT> struct regex_traits;

  // [re.regex], class template basic_regex
  template<class charT, class traits = regex_traits<charT>> class basic_regex;

  using regex  = basic_regex<char>;
  using wregex = basic_regex<wchar_t>;

  // [re.regex.swap], basic_regex swap
  template<class charT, class traits>
    void swap(basic_regex<charT, traits>& e1, basic_regex<charT, traits>& e2);

  // [re.submatch], class template sub_match
  template<class BidirectionalIterator>
    class sub_match;

  using csub_match  = sub_match<const char*>;
  using wcsub_match = sub_match<const wchar_t*>;
  using ssub_match  = sub_match<string::const_iterator>;
  using wssub_match = sub_match<wstring::const_iterator>;

  // [re.submatch.op], sub_match non-member operators
  template<class BiIter>
    bool operator==(const sub_match<BiIter>& lhs, const sub_match<BiIter>& rhs);
  template<class BiIter>
    auto operator<=>(const sub_match<BiIter>& lhs, const sub_match<BiIter>& rhs);

  template<class BiIter, class ST, class SA>
    bool operator==(
      const sub_match<BiIter>& lhs,
      const basic_string<typename iterator_traits<BiIter>::value_type, ST, SA>& rhs);
  template<class BiIter, class ST, class SA>
    auto operator<=>(
      const sub_match<BiIter>& lhs,
      const basic_string<typename iterator_traits<BiIter>::value_type, ST, SA>& rhs);

  template<class BiIter>
    bool operator==(const sub_match<BiIter>& lhs,
                    const typename iterator_traits<BiIter>::value_type* rhs);
  template<class BiIter>
    auto operator<=>(const sub_match<BiIter>& lhs,
                     const typename iterator_traits<BiIter>::value_type* rhs);

  template<class BiIter>
    bool operator==(const sub_match<BiIter>& lhs,
                    const typename iterator_traits<BiIter>::value_type& rhs);
  template<class BiIter>
    auto operator<=>(const sub_match<BiIter>& lhs,
                     const typename iterator_traits<BiIter>::value_type& rhs);

  template<class charT, class ST, class BiIter>
    basic_ostream<charT, ST>&
      operator<<(basic_ostream<charT, ST>& os, const sub_match<BiIter>& m);

  // [re.results], class template match_results
  template<class BidirectionalIterator,
           class Allocator = allocator<sub_match<BidirectionalIterator>>>
    class match_results;

  using cmatch  = match_results<const char*>;
  using wcmatch = match_results<const wchar_t*>;
  using smatch  = match_results<string::const_iterator>;
  using wsmatch = match_results<wstring::const_iterator>;

  // match_results comparisons
  template<class BidirectionalIterator, class Allocator>
    bool operator==(const match_results<BidirectionalIterator, Allocator>& m1,
                    const match_results<BidirectionalIterator, Allocator>& m2);

  // [re.results.swap], match_results swap
  template<class BidirectionalIterator, class Allocator>
    void swap(match_results<BidirectionalIterator, Allocator>& m1,
              match_results<BidirectionalIterator, Allocator>& m2);

  // [re.alg.match], function template regex_match
  template<class BidirectionalIterator, class Allocator, class charT, class traits>
    bool regex_match(BidirectionalIterator first, BidirectionalIterator last,
                     match_results<BidirectionalIterator, Allocator>& m,
                     const basic_regex<charT, traits>& e,
                     regex_constants::match_flag_type flags = regex_constants::match_default);
  template<class BidirectionalIterator, class charT, class traits>
    bool regex_match(BidirectionalIterator first, BidirectionalIterator last,
                     const basic_regex<charT, traits>& e,
                     regex_constants::match_flag_type flags = regex_constants::match_default);
  template<class charT, class Allocator, class traits>
    bool regex_match(const charT* str, match_results<const charT*, Allocator>& m,
                     const basic_regex<charT, traits>& e,
                     regex_constants::match_flag_type flags = regex_constants::match_default);
  template<class ST, class SA, class Allocator, class charT, class traits>
    bool regex_match(const basic_string<charT, ST, SA>& s,
                     match_results<typename basic_string<charT, ST, SA>::const_iterator,
                                   Allocator>& m,
                     const basic_regex<charT, traits>& e,
                     regex_constants::match_flag_type flags = regex_constants::match_default);
  template<class ST, class SA, class Allocator, class charT, class traits>
    bool regex_match(const basic_string<charT, ST, SA>&&,
                     match_results<typename basic_string<charT, ST, SA>::const_iterator,
                                   Allocator>&,
                     const basic_regex<charT, traits>&,
                     regex_constants::match_flag_type = regex_constants::match_default) = delete;
  template<class charT, class traits>
    bool regex_match(const charT* str,
                     const basic_regex<charT, traits>& e,
                     regex_constants::match_flag_type flags = regex_constants::match_default);
  template<class ST, class SA, class charT, class traits>
    bool regex_match(const basic_string<charT, ST, SA>& s,
                     const basic_regex<charT, traits>& e,
                     regex_constants::match_flag_type flags = regex_constants::match_default);

  // [re.alg.search], function template regex_search
  template<class BidirectionalIterator, class Allocator, class charT, class traits>
    bool regex_search(BidirectionalIterator first, BidirectionalIterator last,
                      match_results<BidirectionalIterator, Allocator>& m,
                      const basic_regex<charT, traits>& e,
                      regex_constants::match_flag_type flags = regex_constants::match_default);
  template<class BidirectionalIterator, class charT, class traits>
    bool regex_search(BidirectionalIterator first, BidirectionalIterator last,
                      const basic_regex<charT, traits>& e,
                      regex_constants::match_flag_type flags = regex_constants::match_default);
  template<class charT, class Allocator, class traits>
    bool regex_search(const charT* str,
                      match_results<const charT*, Allocator>& m,
                      const basic_regex<charT, traits>& e,
                      regex_constants::match_flag_type flags = regex_constants::match_default);
  template<class charT, class traits>
    bool regex_search(const charT* str,
                      const basic_regex<charT, traits>& e,
                      regex_constants::match_flag_type flags = regex_constants::match_default);
  template<class ST, class SA, class charT, class traits>
    bool regex_search(const basic_string<charT, ST, SA>& s,
                      const basic_regex<charT, traits>& e,
                      regex_constants::match_flag_type flags = regex_constants::match_default);
  template<class ST, class SA, class Allocator, class charT, class traits>
    bool regex_search(const basic_string<charT, ST, SA>& s,
                      match_results<typename basic_string<charT, ST, SA>::const_iterator,
                                    Allocator>& m,
                      const basic_regex<charT, traits>& e,
                      regex_constants::match_flag_type flags = regex_constants::match_default);
  template<class ST, class SA, class Allocator, class charT, class traits>
    bool regex_search(const basic_string<charT, ST, SA>&&,
                      match_results<typename basic_string<charT, ST, SA>::const_iterator,
                                    Allocator>&,
                      const basic_regex<charT, traits>&,
                      regex_constants::match_flag_type
                        = regex_constants::match_default) = delete;

  // [re.alg.replace], function template regex_replace
  template<class OutputIterator, class BidirectionalIterator,
            class traits, class charT, class ST, class SA>
    OutputIterator
      regex_replace(OutputIterator out,
                    BidirectionalIterator first, BidirectionalIterator last,
                    const basic_regex<charT, traits>& e,
                    const basic_string<charT, ST, SA>& fmt,
                    regex_constants::match_flag_type flags = regex_constants::match_default);
  template<class OutputIterator, class BidirectionalIterator, class traits, class charT>
    OutputIterator
      regex_replace(OutputIterator out,
                    BidirectionalIterator first, BidirectionalIterator last,
                    const basic_regex<charT, traits>& e,
                    const charT* fmt,
                    regex_constants::match_flag_type flags = regex_constants::match_default);
  template<class traits, class charT, class ST, class SA, class FST, class FSA>
    basic_string<charT, ST, SA>
      regex_replace(const basic_string<charT, ST, SA>& s,
                    const basic_regex<charT, traits>& e,
                    const basic_string<charT, FST, FSA>& fmt,
                    regex_constants::match_flag_type flags = regex_constants::match_default);
  template<class traits, class charT, class ST, class SA>
    basic_string<charT, ST, SA>
      regex_replace(const basic_string<charT, ST, SA>& s,
                    const basic_regex<charT, traits>& e,
                    const charT* fmt,
                    regex_constants::match_flag_type flags = regex_constants::match_default);
  template<class traits, class charT, class ST, class SA>
    basic_string<charT>
      regex_replace(const charT* s,
                    const basic_regex<charT, traits>& e,
                    const basic_string<charT, ST, SA>& fmt,
                    regex_constants::match_flag_type flags = regex_constants::match_default);
  template<class traits, class charT>
    basic_string<charT>
      regex_replace(const charT* s,
                    const basic_regex<charT, traits>& e,
                    const charT* fmt,
                    regex_constants::match_flag_type flags = regex_constants::match_default);

  // [re.regiter], class template regex_iterator
  template<class BidirectionalIterator,
            class charT = typename iterator_traits<BidirectionalIterator>::value_type,
            class traits = regex_traits<charT>>
    class regex_iterator;

  using cregex_iterator  = regex_iterator<const char*>;
  using wcregex_iterator = regex_iterator<const wchar_t*>;
  using sregex_iterator  = regex_iterator<string::const_iterator>;
  using wsregex_iterator = regex_iterator<wstring::const_iterator>;

  // [re.tokiter], class template regex_token_iterator
  template<class BidirectionalIterator,
            class charT = typename iterator_traits<BidirectionalIterator>::value_type,
            class traits = regex_traits<charT>>
    class regex_token_iterator;

  using cregex_token_iterator  = regex_token_iterator<const char*>;
  using wcregex_token_iterator = regex_token_iterator<const wchar_t*>;
  using sregex_token_iterator  = regex_token_iterator<string::const_iterator>;
  using wsregex_token_iterator = regex_token_iterator<wstring::const_iterator>;

  namespace pmr {
    template<class BidirectionalIterator>
      using match_results =
        std::match_results<BidirectionalIterator,
                           polymorphic_allocator<sub_match<BidirectionalIterator>>>;

    using cmatch  = match_results<const char*>;
    using wcmatch = match_results<const wchar_t*>;
    using smatch  = match_results<string::const_iterator>;
    using wsmatch = match_results<wstring::const_iterator>;
  }
}
```
