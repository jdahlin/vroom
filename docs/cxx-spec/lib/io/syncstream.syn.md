# 29.10.1 Header `<syncstream>` synopsis [syncstream.syn]

[Contents](../../README.md) · up: [29.10 Synchronized output streams \[syncstream\]](syncstream.md) · prev: [29.10 Synchronized output streams \[syncstream\]](syncstream.md) · next: [29.10.2 Class template `basic_syncbuf` \[syncstream.syncbuf\]](syncstream.syncbuf.md)

```cpp
#include <ostream>  // see [ostream.syn]

namespace std {
  template<class charT, class traits = char_traits<charT>, class Allocator = allocator<charT>>
    class basic_syncbuf;

  using syncbuf = basic_syncbuf<char>;
  using wsyncbuf = basic_syncbuf<wchar_t>;

  template<class charT, class traits = char_traits<charT>, class Allocator = allocator<charT>>
    class basic_osyncstream;

  using osyncstream = basic_osyncstream<char>;
  using wosyncstream = basic_osyncstream<wchar_t>;
}
```

**1** The header `<syncstream>` provides a mechanism to synchronize execution agents writing to the same stream.
