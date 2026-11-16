# 20.20.6.1 Class template `basic_format_arg` [format.arg]

[Contents](../README.md) · up: [20.20.6 Arguments \[format.arguments\]](format.arguments.md) · prev: [20.20.6 Arguments \[format.arguments\]](format.arguments.md) · next: [20.20.6.2 Class template `format-arg-store` \[format.arg.store\]](format.arg.store.md)

```cpp
namespace std {
  template<class Context>
  class basic_format_arg {
  public:
    class handle;

  private:
    using char_type = typename Context::char_type;                              // exposition only

    variant<monostate, bool, char_type,
            int, unsigned int, long long int, unsigned long long int,
            float, double, long double,
            const char_type*, basic_string_view<char_type>,
            const void*, handle> value;                                         // exposition only

    template<class T> explicit basic_format_arg(const T& v) noexcept;           // exposition only
    explicit basic_format_arg(float n) noexcept;                                // exposition only
    explicit basic_format_arg(double n) noexcept;                               // exposition only
    explicit basic_format_arg(long double n) noexcept;                          // exposition only
    explicit basic_format_arg(const char_type* s);                              // exposition only

    template<class traits>
      explicit basic_format_arg(
        basic_string_view<char_type, traits> s) noexcept;                       // exposition only

    template<class traits, class Allocator>
      explicit basic_format_arg(
        const basic_string<char_type, traits, Allocator>& s) noexcept;          // exposition only

    explicit basic_format_arg(nullptr_t) noexcept;                              // exposition only

    template<class T>
      explicit basic_format_arg(const T* p) noexcept;                           // exposition only

  public:
    basic_format_arg() noexcept;

    explicit operator bool() const noexcept;
  };
}
```

**1** An instance of `basic_format_arg` provides access to a formatting argument for user-defined formatters.

**2** The behavior of a program that adds specializations of `basic_format_arg` is undefined.

```cpp
basic_format_arg() noexcept;
```

**3** *Postconditions:* `!(*this)`.

```cpp
template<class T> explicit basic_format_arg(const T& v) noexcept;
```

**4** *Constraints:* The template specialization

```cpp
typename Context::template formatter_type<T>
```

meets the *Formatter* requirements ([[formatter.requirements]](formatter.requirements.md)). The extent to which an implementation determines that the specialization meets the *Formatter* requirements is unspecified, except that as a minimum the expression

```cpp
typename Context::template formatter_type<T>()
  .format(declval<const T&>(), declval<Context&>())
```

shall be well-formed when treated as an unevaluated operand.

**5** *Effects:*

- if `T` is `bool` or `char_type`, initializes `value` with `v`;
- otherwise, if `T` is `char` and `char_type` is `wchar_t`, initializes `value` with `static_cast<wchar_t>(v)`;
- otherwise, if `T` is a signed integer type ([[basic.fundamental]](../basic.fundamental.md)) and `sizeof(T) <= sizeof(int)`, initializes `value` with `static_cast<int>(v)`;
- otherwise, if `T` is an unsigned integer type and `sizeof(T) <= sizeof(unsigned int)`, initializes `value` with `static_cast<unsigned int>(v)`;
- otherwise, if `T` is a signed integer type and `sizeof(T) <= sizeof(long long int)`, initializes `value` with `static_cast<long long int>(v)`;
- otherwise, if `T` is an unsigned integer type and `sizeof(T) <= sizeof(unsigned long long int)`, initializes `value` with `static_cast<unsigned long long int>(v)`;
- otherwise, initializes `value` with `handle(v)`.

```cpp
explicit basic_format_arg(float n) noexcept;
explicit basic_format_arg(double n) noexcept;
explicit basic_format_arg(long double n) noexcept;
```

**6** *Effects:* Initializes `value` with `n`.

```cpp
explicit basic_format_arg(const char_type* s);
```

**7** *Preconditions:* `s` points to a NTCTS ([[defns.ntcts]](definitions.md#defns.ntcts)).

**8** *Effects:* Initializes `value` with `s`.

```cpp
template<class traits>
  explicit basic_format_arg(basic_string_view<char_type, traits> s) noexcept;
```

**9** *Effects:* Initializes `value` with `s`.

```cpp
template<class traits, class Allocator>
  explicit basic_format_arg(
    const basic_string<char_type, traits, Allocator>& s) noexcept;
```

**10** *Effects:* Initializes `value` with `basic_string_view<char_type>(s.data(), s.size())`.

```cpp
explicit basic_format_arg(nullptr_t) noexcept;
```

**11** *Effects:* Initializes `value` with `static_cast<const void*>(nullptr)`.

```cpp
template<class T> explicit basic_format_arg(const T* p) noexcept;
```

**12** *Constraints:* `is_void_v<T>` is `true`.

**13** *Effects:* Initializes `value` with `p`.

**14** [*Note 1*: Constructing `basic_format_arg` from a pointer to a member is ill-formed unless the user provides an enabled specialization of `formatter` for that pointer to member type. — *end note*]

```cpp
explicit operator bool() const noexcept;
```

**15** *Returns:* `!holds_alternative<monostate>(value)`.

**16** The class `handle` allows formatting an object of a user-defined type.

```cpp
namespace std {
  template<class Context>
  class basic_format_arg<Context>::handle {
    const void* ptr_;                                           // exposition only
    void (*format_)(basic_format_parse_context<char_type>&,
                    Context&, const void*);                     // exposition only

    template<class T> explicit handle(const T& val) noexcept;   // exposition only

    friend class basic_format_arg<Context>;                     // exposition only

  public:
    void format(basic_format_parse_context<char_type>&, Context& ctx) const;
  };
}
```

```cpp
template<class T> explicit handle(const T& val) noexcept;
```

**17** *Effects:* Initializes `ptr_` with `addressof(val)` and `format_` with

```cpp
[](basic_format_parse_context<char_type>& parse_ctx,
   Context& format_ctx, const void* ptr) {
  typename Context::template formatter_type<T> f;
  parse_ctx.advance_to(f.parse(parse_ctx));
  format_ctx.advance_to(f.format(*static_cast<const T*>(ptr), format_ctx));
}
```

```cpp
void format(basic_format_parse_context<char_type>& parse_ctx, Context& format_ctx) const;
```

**18** *Effects:* Equivalent to: `format_(parse_ctx, format_ctx, ptr_);`

```cpp
template<class Visitor, class Context>
  see below visit_format_arg(Visitor&& vis, basic_format_arg<Context> arg);
```

**19** *Effects:* Equivalent to: `return visit(forward<Visitor>(vis), arg.value);`
