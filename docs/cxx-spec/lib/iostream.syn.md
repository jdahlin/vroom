# 29.4.1 Header `<iostream>` synopsis [iostream.syn]

[Contents](../README.md) · up: [29.4 Standard iostream objects \[iostream.objects\]](iostream.objects.md) · prev: [29.4 Standard iostream objects \[iostream.objects\]](iostream.objects.md) · next: [29.4.2 Overview \[iostream.objects.overview\]](iostream.objects.overview.md)

```cpp
#include <ios>          // see [ios.syn]
#include <streambuf>    // see [streambuf.syn]
#include <istream>      // see [istream.syn]
#include <ostream>      // see [ostream.syn]

namespace std {
  extern istream cin;
  extern ostream cout;
  extern ostream cerr;
  extern ostream clog;

  extern wistream wcin;
  extern wostream wcout;
  extern wostream wcerr;
  extern wostream wclog;
}
```
