# 29.7.4.1 Class template `basic_istream` [istream]

[Contents](../README.md) · up: [29.7.4 Input streams \[input.streams\]](input.streams.md) · prev: [29.7.4 Input streams \[input.streams\]](input.streams.md) · next: [29.7.4.1.1 Constructors \[istream.cons\]](istream.cons.md)

```cpp
namespace std {
  template<class charT, class traits = char_traits<charT>>
  class basic_istream : virtual public basic_ios<charT, traits> {
  public:
    // types (inherited from basic_ios ([ios]))
    using char_type   = charT;
    using int_type    = typename traits::int_type;
    using pos_type    = typename traits::pos_type;
    using off_type    = typename traits::off_type;
    using traits_type = traits;

    // [istream.cons], constructor/destructor
    explicit basic_istream(basic_streambuf<charT, traits>* sb);
    virtual ~basic_istream();

    // [istream.sentry], prefix/suffix
    class sentry;

    // [istream.formatted], formatted input
    basic_istream<charT, traits>&
      operator>>(basic_istream<charT, traits>& (*pf)(basic_istream<charT, traits>&));
    basic_istream<charT, traits>&
      operator>>(basic_ios<charT, traits>& (*pf)(basic_ios<charT, traits>&));
    basic_istream<charT, traits>&
      operator>>(ios_base& (*pf)(ios_base&));

    basic_istream<charT, traits>& operator>>(bool& n);
    basic_istream<charT, traits>& operator>>(short& n);
    basic_istream<charT, traits>& operator>>(unsigned short& n);
    basic_istream<charT, traits>& operator>>(int& n);
    basic_istream<charT, traits>& operator>>(unsigned int& n);
    basic_istream<charT, traits>& operator>>(long& n);
    basic_istream<charT, traits>& operator>>(unsigned long& n);
    basic_istream<charT, traits>& operator>>(long long& n);
    basic_istream<charT, traits>& operator>>(unsigned long long& n);
    basic_istream<charT, traits>& operator>>(float& f);
    basic_istream<charT, traits>& operator>>(double& f);
    basic_istream<charT, traits>& operator>>(long double& f);

    basic_istream<charT, traits>& operator>>(void*& p);
    basic_istream<charT, traits>& operator>>(basic_streambuf<char_type, traits>* sb);

    // [istream.unformatted], unformatted input
    streamsize gcount() const;
    int_type get();
    basic_istream<charT, traits>& get(char_type& c);
    basic_istream<charT, traits>& get(char_type* s, streamsize n);
    basic_istream<charT, traits>& get(char_type* s, streamsize n, char_type delim);
    basic_istream<charT, traits>& get(basic_streambuf<char_type, traits>& sb);
    basic_istream<charT, traits>& get(basic_streambuf<char_type, traits>& sb, char_type delim);

    basic_istream<charT, traits>& getline(char_type* s, streamsize n);
    basic_istream<charT, traits>& getline(char_type* s, streamsize n, char_type delim);

    basic_istream<charT, traits>& ignore(streamsize n = 1, int_type delim = traits::eof());
    int_type                      peek();
    basic_istream<charT, traits>& read    (char_type* s, streamsize n);
    streamsize                    readsome(char_type* s, streamsize n);

    basic_istream<charT, traits>& putback(char_type c);
    basic_istream<charT, traits>& unget();
    int sync();

    pos_type tellg();
    basic_istream<charT, traits>& seekg(pos_type);
    basic_istream<charT, traits>& seekg(off_type, ios_base::seekdir);

  protected:
    // [istream.cons], copy/move constructor
    basic_istream(const basic_istream&) = delete;
    basic_istream(basic_istream&& rhs);

    // [istream.assign], assign and swap
    basic_istream& operator=(const basic_istream&) = delete;
    basic_istream& operator=(basic_istream&& rhs);
    void swap(basic_istream& rhs);
  };

  // [istream.extractors], character extraction templates
  template<class charT, class traits>
    basic_istream<charT, traits>& operator>>(basic_istream<charT, traits>&, charT&);
  template<class traits>
    basic_istream<char, traits>& operator>>(basic_istream<char, traits>&, unsigned char&);
  template<class traits>
    basic_istream<char, traits>& operator>>(basic_istream<char, traits>&, signed char&);

  template<class charT, class traits, size_t N>
    basic_istream<charT, traits>& operator>>(basic_istream<charT, traits>&, charT(&)[N]);
  template<class traits, size_t N>
    basic_istream<char, traits>& operator>>(basic_istream<char, traits>&, unsigned char(&)[N]);
  template<class traits, size_t N>
    basic_istream<char, traits>& operator>>(basic_istream<char, traits>&, signed char(&)[N]);
}
```

**1** The class template `basic_istream` defines a number of member function signatures that assist in reading and interpreting input from sequences controlled by a stream buffer.

**2** Two groups of member function signatures share common properties: the *formatted input functions* (or *extractors*) and the *unformatted input functions.* Both groups of input functions are described as if they obtain (or *extract*) input *characters* by calling `rdbuf()->sbumpc()` or `rdbuf()->sgetc()`. They may use other public members of `istream`.

**3** If `rdbuf()->sbumpc()` or `rdbuf()->sgetc()` returns `traits::eof()`, then the input function, except as explicitly noted otherwise, completes its actions and does `setstate(eofbit)`, which may throw `ios_base::failure` ([[iostate.flags]](iostate.flags.md)), before returning.

**4** If one of these called functions throws an exception, then unless explicitly noted otherwise, the input function sets `badbit` in the error state. If `badbit` is set in `exceptions()`, the input function rethrows the exception without completing its actions, otherwise it does not throw anything and proceeds as if the called function had returned a failure indication.

## Subclauses

- [29.7.4.1.1 Constructors \[istream.cons\]](istream.cons.md)
- [29.7.4.1.2 Assignment and swap \[istream.assign\]](istream.assign.md)
- [29.7.4.1.3 Class `basic_istream::sentry` \[istream.sentry\]](istream.sentry.md)
