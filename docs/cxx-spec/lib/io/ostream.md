# 29.7.5.1 Class template `basic_ostream` [ostream]

[Contents](../../README.md) · up: [29.7.5 Output streams \[output.streams\]](output.streams.md) · prev: [29.7.5 Output streams \[output.streams\]](output.streams.md) · next: [29.7.5.1.1 Constructors \[ostream.cons\]](ostream.cons.md)

```cpp
namespace std {
  template<class charT, class traits = char_traits<charT>>
  class basic_ostream : virtual public basic_ios<charT, traits> {
  public:
    // types (inherited from basic_ios ([ios]))
    using char_type   = charT;
    using int_type    = typename traits::int_type;
    using pos_type    = typename traits::pos_type;
    using off_type    = typename traits::off_type;
    using traits_type = traits;

    // [ostream.cons], constructor/destructor
    explicit basic_ostream(basic_streambuf<char_type, traits>* sb);
    virtual ~basic_ostream();

    // [ostream.sentry], prefix/suffix
    class sentry;

    // [ostream.formatted], formatted output
    basic_ostream<charT, traits>&
      operator<<(basic_ostream<charT, traits>& (*pf)(basic_ostream<charT, traits>&));
    basic_ostream<charT, traits>&
      operator<<(basic_ios<charT, traits>& (*pf)(basic_ios<charT, traits>&));
    basic_ostream<charT, traits>&
      operator<<(ios_base& (*pf)(ios_base&));

    basic_ostream<charT, traits>& operator<<(bool n);
    basic_ostream<charT, traits>& operator<<(short n);
    basic_ostream<charT, traits>& operator<<(unsigned short n);
    basic_ostream<charT, traits>& operator<<(int n);
    basic_ostream<charT, traits>& operator<<(unsigned int n);
    basic_ostream<charT, traits>& operator<<(long n);
    basic_ostream<charT, traits>& operator<<(unsigned long n);
    basic_ostream<charT, traits>& operator<<(long long n);
    basic_ostream<charT, traits>& operator<<(unsigned long long n);
    basic_ostream<charT, traits>& operator<<(float f);
    basic_ostream<charT, traits>& operator<<(double f);
    basic_ostream<charT, traits>& operator<<(long double f);

    basic_ostream<charT, traits>& operator<<(const void* p);
    basic_ostream<charT, traits>& operator<<(nullptr_t);
    basic_ostream<charT, traits>& operator<<(basic_streambuf<char_type, traits>* sb);

    // [ostream.unformatted], unformatted output
    basic_ostream<charT, traits>& put(char_type c);
    basic_ostream<charT, traits>& write(const char_type* s, streamsize n);

    basic_ostream<charT, traits>& flush();

    // [ostream.seeks], seeks
    pos_type tellp();
    basic_ostream<charT, traits>& seekp(pos_type);
    basic_ostream<charT, traits>& seekp(off_type, ios_base::seekdir);

  protected:
    // [ostream.cons], copy/move constructor
    basic_ostream(const basic_ostream&) = delete;
    basic_ostream(basic_ostream&& rhs);

    // [ostream.assign], assign and swap
    basic_ostream& operator=(const basic_ostream&) = delete;
    basic_ostream& operator=(basic_ostream&& rhs);
    void swap(basic_ostream& rhs);
  };

  // [ostream.inserters.character], character inserters
  template<class charT, class traits>
    basic_ostream<charT, traits>& operator<<(basic_ostream<charT, traits>&, charT);
  template<class charT, class traits>
    basic_ostream<charT, traits>& operator<<(basic_ostream<charT, traits>&, char);
  template<class traits>
    basic_ostream<char, traits>& operator<<(basic_ostream<char, traits>&, char);

  template<class traits>
    basic_ostream<char, traits>& operator<<(basic_ostream<char, traits>&, signed char);
  template<class traits>
    basic_ostream<char, traits>& operator<<(basic_ostream<char, traits>&, unsigned char);

  template<class traits>
    basic_ostream<char, traits>& operator<<(basic_ostream<char, traits>&, wchar_t) = delete;
  template<class traits>
    basic_ostream<char, traits>& operator<<(basic_ostream<char, traits>&, char8_t) = delete;
  template<class traits>
    basic_ostream<char, traits>& operator<<(basic_ostream<char, traits>&, char16_t) = delete;
  template<class traits>
    basic_ostream<char, traits>& operator<<(basic_ostream<char, traits>&, char32_t) = delete;
  template<class traits>
    basic_ostream<wchar_t, traits>&
      operator<<(basic_ostream<wchar_t, traits>&, char8_t) = delete;
  template<class traits>
    basic_ostream<wchar_t, traits>&
      operator<<(basic_ostream<wchar_t, traits>&, char16_t) = delete;
  template<class traits>
    basic_ostream<wchar_t, traits>&
      operator<<(basic_ostream<wchar_t, traits>&, char32_t) = delete;

  template<class charT, class traits>
    basic_ostream<charT, traits>& operator<<(basic_ostream<charT, traits>&, const charT*);
  template<class charT, class traits>
    basic_ostream<charT, traits>& operator<<(basic_ostream<charT, traits>&, const char*);
  template<class traits>
    basic_ostream<char, traits>& operator<<(basic_ostream<char, traits>&, const char*);

  template<class traits>
    basic_ostream<char, traits>& operator<<(basic_ostream<char, traits>&, const signed char*);
  template<class traits>
    basic_ostream<char, traits>& operator<<(basic_ostream<char, traits>&, const unsigned char*);

  template<class traits>
    basic_ostream<char, traits>&
      operator<<(basic_ostream<char, traits>&, const wchar_t*) = delete;
  template<class traits>
    basic_ostream<char, traits>&
      operator<<(basic_ostream<char, traits>&, const char8_t*) = delete;
  template<class traits>
    basic_ostream<char, traits>&
      operator<<(basic_ostream<char, traits>&, const char16_t*) = delete;
  template<class traits>
    basic_ostream<char, traits>&
      operator<<(basic_ostream<char, traits>&, const char32_t*) = delete;
  template<class traits>
    basic_ostream<wchar_t, traits>&
      operator<<(basic_ostream<wchar_t, traits>&, const char8_t*) = delete;
  template<class traits>
    basic_ostream<wchar_t, traits>&
      operator<<(basic_ostream<wchar_t, traits>&, const char16_t*) = delete;
  template<class traits>
    basic_ostream<wchar_t, traits>&
      operator<<(basic_ostream<wchar_t, traits>&, const char32_t*) = delete;
}
```

**1** The class template `basic_ostream` defines a number of member function signatures that assist in formatting and writing output to output sequences controlled by a stream buffer.

**2** Two groups of member function signatures share common properties: the *formatted output functions* (or *inserters*) and the *unformatted output functions.* Both groups of output functions generate (or *insert*) output *characters* by actions equivalent to calling `rdbuf()->sputc(int_type)`. They may use other public members of `basic_ostream` except that they shall not invoke any virtual members of `rdbuf()` except `overflow()`, `xsputn()`, and `sync()`.

**3** If one of these called functions throws an exception, then unless explicitly noted otherwise the output function sets `badbit` in the error state. If `badbit` is set in `exceptions()`, the output function rethrows the exception without completing its actions, otherwise it does not throw anything and proceeds as if the called function had returned a failure indication.

**4** [*Note 1*: The deleted overloads of `operator<<` prevent formatting characters as integers and strings as pointers. — *end note*]

## Subclauses

- [29.7.5.1.1 Constructors \[ostream.cons\]](ostream.cons.md)
- [29.7.5.1.2 Assignment and swap \[ostream.assign\]](ostream.assign.md)
- [29.7.5.1.3 Class `basic_ostream::sentry` \[ostream.sentry\]](ostream.sentry.md)
- [29.7.5.1.4 Seek members \[ostream.seeks\]](ostream.seeks.md)
