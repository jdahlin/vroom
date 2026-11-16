# 20.20.5.4 Class template `basic_format_context` [format.context]

[Contents](../README.md) · up: [20.20.5 Formatter \[format.formatter\]](format.formatter.md) · prev: [20.20.5.3 Class template `basic_format_parse_context` \[format.parse.ctx\]](format.parse.ctx.md) · next: [20.20.6 Arguments \[format.arguments\]](format.arguments.md)

```cpp
namespace std {
  template<class Out, class charT>
  class basic_format_context {
    basic_format_args<basic_format_context> args_;      // exposition only
    Out out_;                                           // exposition only

  public:
    using iterator = Out;
    using char_type = charT;
    template<class T> using formatter_type = formatter<T, charT>;

    basic_format_arg<basic_format_context> arg(size_t id) const;
    std::locale locale();

    iterator out();
    void advance_to(iterator it);
  };
}
```

**1** An instance of `basic_format_context` holds formatting state consisting of the formatting arguments and the output iterator.

**2** `Out` shall model `output_iterator<const charT&>`.

**3** `format_context` is an alias for a specialization of `basic_format_context` with an output iterator that appends to `string`, such as `back_insert_iterator<string>`. Similarly, `wformat_context` is an alias for a specialization of `basic_format_context` with an output iterator that appends to `wstring`.

**4** [*Note 1*: For a given type `charT`, implementations are encouraged to provide a single instantiation of `basic_format_context` for appending to `basic_string<charT>`, `vector<charT>`, or any other container with contiguous storage by wrapping those in temporary objects with a uniform interface (such as a `span<charT>`) and polymorphic reallocation. — *end note*]

```cpp
basic_format_arg<basic_format_context> arg(size_t id) const;
```

**5** *Returns:* `args_.get(id)`.

```cpp
std::locale locale();
```

**6** *Returns:* The locale passed to the formatting function if the latter takes one, and `std::locale()` otherwise.

```cpp
iterator out();
```

**7** *Returns:* `out_`.

```cpp
void advance_to(iterator it);
```

**8** *Effects:* Equivalent to: `out_ = it;`

[*Example 1*:

```cpp
struct S { int value; };

template<> struct std::formatter<S> {
  size_t width_arg_id = 0;

  // Parses a width argument id in the format { digit }.
  constexpr auto parse(format_parse_context& ctx) {
    auto iter = ctx.begin();
    auto get_char = [&]() { return iter != ctx.end() ? *iter : 0; };
    if (get_char() != '{')
      return iter;
    ++iter;
    char c = get_char();
    if (!isdigit(c) || (++iter, get_char()) != '}')
      throw format_error("invalid format");
    width_arg_id = c - '0';
    ctx.check_arg_id(width_arg_id);
    return ++iter;
  }

  // Formats an S with width given by the argument width_arg_id.
  auto format(S s, format_context& ctx) {
    int width = visit_format_arg([](auto value) -> int {
      if constexpr (!is_integral_v<decltype(value)>)
        throw format_error("width is not integral");
      else if (value < 0 || value > numeric_limits<int>::max())
        throw format_error("invalid width");
      else
        return value;
      }, ctx.arg(width_arg_id));
    return format_to(ctx.out(), "{0:x<{1}}", s.value, width);
  }
};

std::string s = std::format("{0:{1}}", S{42}, 10);  // value of s is "xxxxxxxx42"
```

— *end example*]
