# 29.4.4 Wide stream objects [wide.stream.objects]

[Contents](../../README.md) · up: [29.4 Standard iostream objects \[iostream.objects\]](iostream.objects.md) · prev: [29.4.3 Narrow stream objects \[narrow.stream.objects\]](narrow.stream.objects.md) · next: [29.5 Iostreams base classes \[iostreams.base\]](iostreams.base.md)

```cpp
wistream wcin;
```

**1** The object `wcin` controls input from a stream buffer associated with the object `stdin`, declared in `<cstdio>` ([[cstdio.syn]](cstdio.syn.md)).

**2** After the object `wcin` is initialized, `wcin.tie()` returns `&wcout`. Its state is otherwise the same as required for `basic_ios<wchar_t>::init` ([[basic.ios.cons]](basic.ios.cons.md)).

```cpp
wostream wcout;
```

**3** The object `wcout` controls output to a stream buffer associated with the object `stdout`, declared in `<cstdio>` ([[cstdio.syn]](cstdio.syn.md)).

```cpp
wostream wcerr;
```

**4** The object `wcerr` controls output to a stream buffer associated with the object `stderr`, declared in `<cstdio>` ([[cstdio.syn]](cstdio.syn.md)).

**5** After the object `wcerr` is initialized, `wcerr.flags() & unitbuf` is nonzero and `wcerr.tie()` returns `&wcout`. Its state is otherwise the same as required for `basic_ios<wchar_t>::init` ([[basic.ios.cons]](basic.ios.cons.md)).

```cpp
wostream wclog;
```

**6** The object `wclog` controls output to a stream buffer associated with the object `stderr`, declared in `<cstdio>` ([[cstdio.syn]](cstdio.syn.md)).
