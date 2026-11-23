# 20.20.5.3 Class template `basic_format_parse_context` [format.parse.ctx]

[Contents](../../README.md) · up: [20.20.5 Formatter \[format.formatter\]](format.formatter.md) · prev: [20.20.5.2 Formatter specializations \[format.formatter.spec\]](format.formatter.spec.md) · next: [20.20.5.4 Class template `basic_format_context` \[format.context\]](format.context.md)

```cpp
namespace std {
  template<class charT>
  class basic_format_parse_context {
  public:
    using char_type = charT;
    using const_iterator = typename basic_string_view<charT>::const_iterator;
    using iterator = const_iterator;

  private:
    iterator begin_;                                    // exposition only
    iterator end_;                                      // exposition only
    enum indexing { unknown, manual, automatic };       // exposition only
    indexing indexing_;                                 // exposition only
    size_t next_arg_id_;                                // exposition only
    size_t num_args_;                                   // exposition only

  public:
    constexpr explicit basic_format_parse_context(basic_string_view<charT> fmt,
                                                  size_t num_args = 0) noexcept;
    basic_format_parse_context(const basic_format_parse_context&) = delete;
    basic_format_parse_context& operator=(const basic_format_parse_context&) = delete;

    constexpr const_iterator begin() const noexcept;
    constexpr const_iterator end() const noexcept;
    constexpr void advance_to(const_iterator it);

    constexpr size_t next_arg_id();
    constexpr void check_arg_id(size_t id);
  };
}
```

**1** An instance of `basic_format_parse_context` holds the format string parsing state consisting of the format string range being parsed and the argument counter for automatic indexing.

```cpp
constexpr explicit basic_format_parse_context(basic_string_view<charT> fmt,
                                              size_t num_args = 0) noexcept;
```

**2** *Effects:* Initializes `begin_` with `fmt.begin()`, `end_` with `fmt.end()`, `indexing_` with `unknown`, `next_arg_id_` with `0`, and `num_args_` with `num_args`.

```cpp
constexpr const_iterator begin() const noexcept;
```

**3** *Returns:* `begin_`.

```cpp
constexpr const_iterator end() const noexcept;
```

**4** *Returns:* `end_`.

```cpp
constexpr void advance_to(const_iterator it);
```

**5** *Preconditions:* `end()` is reachable from `it`.

**6** *Effects:* Equivalent to: `begin_ = it;`

```cpp
constexpr size_t next_arg_id();
```

**7** *Effects:* If `indexing_ != manual`, equivalent to:

```cpp
if (indexing_ == unknown)
  indexing_ = automatic;
return next_arg_id_++;
```

**8** *Throws:* `format_error` if `indexing_ == manual` which indicates mixing of automatic and manual argument indexing.

```cpp
constexpr void check_arg_id(size_t id);
```

**9** *Effects:* If `indexing_ != automatic`, equivalent to:

```cpp
if (indexing_ == unknown)
  indexing_ = manual;
```

**10** *Throws:* `format_error` if `indexing_ == automatic` which indicates mixing of automatic and manual argument indexing.

**11** *Remarks:* Call expressions where `id >= num_args_` are not core constant expressions ([[expr.const]](../../expr/expr.const.md)).
