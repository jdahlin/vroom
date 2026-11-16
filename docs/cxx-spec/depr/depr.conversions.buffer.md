# D.21.2 Class template `wbuffer_convert` [depr.conversions.buffer]

[Contents](../README.md) · up: [D.21 Deprecated convenience conversion interfaces \[depr.conversions\]](depr.conversions.md) · prev: [D.21.1 Class template `wstring_convert` \[depr.conversions.string\]](depr.conversions.string.md) · next: [D.22 Deprecated locale category facets \[depr.locale.category\]](depr.locale.category.md)

**1** Class template `wbuffer_convert` looks like a wide stream buffer, but performs all its I/O through an underlying byte stream buffer that you specify when you construct it. Like class template `wstring_convert`, it lets you specify a code conversion facet to perform the conversions, without affecting any streams or locales.

```cpp
namespace std {
  template<class Codecvt, class Elem = wchar_t, class Tr = char_traits<Elem>>
    class wbuffer_convert : public basic_streambuf<Elem, Tr> {
    public:
      using state_type = typename Codecvt::state_type;

      wbuffer_convert() : wbuffer_convert(nullptr) {}
      explicit wbuffer_convert(streambuf* bytebuf,
                               Codecvt* pcvt = new Codecvt,
                               state_type state = state_type());

      ~wbuffer_convert();

      wbuffer_convert(const wbuffer_convert&) = delete;
      wbuffer_convert& operator=(const wbuffer_convert&) = delete;

      streambuf* rdbuf() const;
      streambuf* rdbuf(streambuf* bytebuf);

      state_type state() const;

    private:
      streambuf* bufptr;            // exposition only
      Codecvt* cvtptr;              // exposition only
      state_type cvtstate;          // exposition only
  };
}
```

**2** The class template describes a stream buffer that controls the transmission of elements of type `Elem`, whose character traits are described by the class `Tr`, to and from a byte stream buffer of type `streambuf`. Conversion between a sequence of `Elem` values and multibyte sequences is performed by an object of class `Codecvt`, which shall meet the requirements of the standard code-conversion facet `codecvt<Elem, char, mbstate_t>`.

**3** An object of this class template stores:

- `bufptr` — a pointer to its underlying byte stream buffer
- `cvtptr` — a pointer to the allocated conversion object (which is freed when the `wbuffer_convert` object is destroyed)
- `cvtstate` — a conversion state object

```cpp
state_type state() const;
```

**4** *Returns:* `cvtstate`.

```cpp
streambuf* rdbuf() const;
```

**5** *Returns:* `bufptr`.

```cpp
streambuf* rdbuf(streambuf* bytebuf);
```

**6** *Effects:* Stores `bytebuf` in `bufptr`.

**7** *Returns:* The previous value of `bufptr`.

```cpp
using state_type = typename Codecvt::state_type;
```

**8** The type shall be a synonym for `Codecvt::state_type`.

```cpp
explicit wbuffer_convert(
    streambuf* bytebuf,
    Codecvt* pcvt = new Codecvt,
    state_type state = state_type());
```

**9** *Requires:* `pcvt != nullptr`.

**10** *Effects:* The constructor constructs a stream buffer object, initializes `bufptr` to `bytebuf`, initializes `cvtptr` to `pcvt`, and initializes `cvtstate` to `state`.

```cpp
~wbuffer_convert();
```

**11** *Effects:* The destructor shall delete `cvtptr`.
