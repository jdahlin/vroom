# 20.20.1 Header `<format>` synopsis [format.syn]

[Contents](../../README.md) · up: [20.20 Formatting \[format\]](format.md) · prev: [20.20 Formatting \[format\]](format.md) · next: [20.20.2 Format string \[format.string\]](format.string.md)

```cpp
namespace std {
  // [format.functions], formatting functions
  template<class... Args>
    string format(string_view fmt, const Args&... args);
  template<class... Args>
    wstring format(wstring_view fmt, const Args&... args);
  template<class... Args>
    string format(const locale& loc, string_view fmt, const Args&... args);
  template<class... Args>
    wstring format(const locale& loc, wstring_view fmt, const Args&... args);

  string vformat(string_view fmt, format_args args);
  wstring vformat(wstring_view fmt, wformat_args args);
  string vformat(const locale& loc, string_view fmt, format_args args);
  wstring vformat(const locale& loc, wstring_view fmt, wformat_args args);

  template<class Out, class... Args>
    Out format_to(Out out, string_view fmt, const Args&... args);
  template<class Out, class... Args>
    Out format_to(Out out, wstring_view fmt, const Args&... args);
  template<class Out, class... Args>
    Out format_to(Out out, const locale& loc, string_view fmt, const Args&... args);
  template<class Out, class... Args>
    Out format_to(Out out, const locale& loc, wstring_view fmt, const Args&... args);

  template<class Out>
    Out vformat_to(Out out, string_view fmt,
                   format_args_t<type_identity_t<Out>, char> args);
  template<class Out>
    Out vformat_to(Out out, wstring_view fmt,
                   format_args_t<type_identity_t<Out>, wchar_t> args);
  template<class Out>
    Out vformat_to(Out out, const locale& loc, string_view fmt,
                   format_args_t<type_identity_t<Out>, char> args);
  template<class Out>
    Out vformat_to(Out out, const locale& loc, wstring_view fmt,
                   format_args_t<type_identity_t<Out>, wchar_t> args);

  template<class Out> struct format_to_n_result {
    Out out;
    iter_difference_t<Out> size;
  };
  template<class Out, class... Args>
    format_to_n_result<Out> format_to_n(Out out, iter_difference_t<Out> n,
                                        string_view fmt, const Args&... args);
  template<class Out, class... Args>
    format_to_n_result<Out> format_to_n(Out out, iter_difference_t<Out> n,
                                        wstring_view fmt, const Args&... args);
  template<class Out, class... Args>
    format_to_n_result<Out> format_to_n(Out out, iter_difference_t<Out> n,
                                        const locale& loc, string_view fmt,
                                        const Args&... args);
  template<class Out, class... Args>
    format_to_n_result<Out> format_to_n(Out out, iter_difference_t<Out> n,
                                        const locale& loc, wstring_view fmt,
                                        const Args&... args);

  template<class... Args>
    size_t formatted_size(string_view fmt, const Args&... args);
  template<class... Args>
    size_t formatted_size(wstring_view fmt, const Args&... args);
  template<class... Args>
    size_t formatted_size(const locale& loc, string_view fmt, const Args&... args);
  template<class... Args>
    size_t formatted_size(const locale& loc, wstring_view fmt, const Args&... args);

  // [format.formatter], formatter
  template<class T, class charT = char> struct formatter;

  // [format.parse.ctx], class template basic_format_parse_context
  template<class charT> class basic_format_parse_context;
  using format_parse_context = basic_format_parse_context<char>;
  using wformat_parse_context = basic_format_parse_context<wchar_t>;

  template<class Out, class charT> class basic_format_context;
  using format_context = basic_format_context<unspecified, char>;
  using wformat_context = basic_format_context<unspecified, wchar_t>;

  // [format.arguments], arguments
  // [format.arg], class template basic_format_arg
  template<class Context> class basic_format_arg;

  template<class Visitor, class Context>
    see below visit_format_arg(Visitor&& vis, basic_format_arg<Context> arg);

  // [format.arg.store], class template format-arg-store
  template<class Context, class... Args> struct format-arg-store;      // exposition only

  template<class Context = format_context, class... Args>
    format-arg-store<Context, Args...>
      make_format_args(const Args&... args);
  template<class... Args>
    format-arg-store<wformat_context, Args...>
      make_wformat_args(const Args&... args);

  // [format.args], class template basic_format_args
  template<class Context> class basic_format_args;
  using format_args = basic_format_args<format_context>;
  using wformat_args = basic_format_args<wformat_context>;

  template<class Out, class charT>
    using format_args_t = basic_format_args<basic_format_context<Out, charT>>;

  // [format.error], class format_error
  class format_error;
}
```

**1** The class template `format_to_n_result` has the template parameters, data members, and special members specified above. It has no base classes or members other than those specified.
