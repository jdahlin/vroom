# 30.8 Class template `basic_regex` [re.regex]

[Contents](../README.md) · up: [30 Regular expressions library \[re\]](re.md) · prev: [30.7 Class template `regex_traits` \[re.traits\]](re.traits.md) · next: [30.8.1 Constructors \[re.regex.construct\]](re.regex.construct.md)

**1** For a char-like type `charT`, specializations of class template `basic_regex` represent regular expressions constructed from character sequences of `charT` characters. In the rest of [[re.regex]](re.regex.md), `charT` denotes a given char-like type. Storage for a regular expression is allocated and freed as necessary by the member functions of class `basic_regex`.

**2** Objects of type specialization of `basic_regex` are responsible for converting the sequence of `charT` objects to an internal representation. It is not specified what form this representation takes, nor how it is accessed by algorithms that operate on regular expressions. [*Note 1*: Implementations will typically declare some function templates as friends of `basic_regex` to achieve this. — *end note*]

**3** The functions described in this Clause report errors by throwing exceptions of type `regex_error`.

```cpp
namespace std {
  template<class charT, class traits = regex_traits<charT>>
    class basic_regex {
    public:
      // types
      using value_type  =          charT;
      using traits_type =          traits;
      using string_type = typename traits::string_type;
      using flag_type   =          regex_constants::syntax_option_type;
      using locale_type = typename traits::locale_type;

      // [re.synopt], constants
      static constexpr flag_type icase = regex_constants::icase;
      static constexpr flag_type nosubs = regex_constants::nosubs;
      static constexpr flag_type optimize = regex_constants::optimize;
      static constexpr flag_type collate = regex_constants::collate;
      static constexpr flag_type ECMAScript = regex_constants::ECMAScript;
      static constexpr flag_type basic = regex_constants::basic;
      static constexpr flag_type extended = regex_constants::extended;
      static constexpr flag_type awk = regex_constants::awk;
      static constexpr flag_type grep = regex_constants::grep;
      static constexpr flag_type egrep = regex_constants::egrep;
      static constexpr flag_type multiline = regex_constants::multiline;

      // [re.regex.construct], construct/copy/destroy
      basic_regex();
      explicit basic_regex(const charT* p, flag_type f = regex_constants::ECMAScript);
      basic_regex(const charT* p, size_t len, flag_type f = regex_constants::ECMAScript);
      basic_regex(const basic_regex&);
      basic_regex(basic_regex&&) noexcept;
      template<class ST, class SA>
        explicit basic_regex(const basic_string<charT, ST, SA>& s,
                             flag_type f = regex_constants::ECMAScript);
      template<class ForwardIterator>
        basic_regex(ForwardIterator first, ForwardIterator last,
                    flag_type f = regex_constants::ECMAScript);
      basic_regex(initializer_list<charT> il, flag_type f = regex_constants::ECMAScript);

      ~basic_regex();

      // [re.regex.assign], assign
      basic_regex& operator=(const basic_regex& e);
      basic_regex& operator=(basic_regex&& e) noexcept;
      basic_regex& operator=(const charT* p);
      basic_regex& operator=(initializer_list<charT> il);
      template<class ST, class SA>
        basic_regex& operator=(const basic_string<charT, ST, SA>& s);

      basic_regex& assign(const basic_regex& e);
      basic_regex& assign(basic_regex&& e) noexcept;
      basic_regex& assign(const charT* p, flag_type f = regex_constants::ECMAScript);
      basic_regex& assign(const charT* p, size_t len, flag_type f = regex_constants::ECMAScript);
      template<class ST, class SA>
        basic_regex& assign(const basic_string<charT, ST, SA>& s,
                            flag_type f = regex_constants::ECMAScript);
      template<class InputIterator>
        basic_regex& assign(InputIterator first, InputIterator last,
                            flag_type f = regex_constants::ECMAScript);
      basic_regex& assign(initializer_list<charT>,
                          flag_type f = regex_constants::ECMAScript);

      // [re.regex.operations], const operations
      unsigned mark_count() const;
      flag_type flags() const;

      // [re.regex.locale], locale
      locale_type imbue(locale_type loc);
      locale_type getloc() const;

      // [re.regex.swap], swap
      void swap(basic_regex&);
    };

  template<class ForwardIterator>
    basic_regex(ForwardIterator, ForwardIterator,
                regex_constants::syntax_option_type = regex_constants::ECMAScript)
      -> basic_regex<typename iterator_traits<ForwardIterator>::value_type>;
}
```

## Subclauses

- [30.8.1 Constructors \[re.regex.construct\]](re.regex.construct.md)
- [30.8.2 Assignment \[re.regex.assign\]](re.regex.assign.md)
- [30.8.3 Constant operations \[re.regex.operations\]](re.regex.operations.md)
- [30.8.4 Locale \[re.regex.locale\]](re.regex.locale.md)
- [30.8.5 Swap \[re.regex.swap\]](re.regex.swap.md)
- [30.8.6 Non-member functions \[re.regex.nonmemb\]](re.regex.nonmemb.md)
