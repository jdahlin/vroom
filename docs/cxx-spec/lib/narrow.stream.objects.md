# 29.4.3 Narrow stream objects [narrow.stream.objects]

[Contents](../README.md) · up: [29.4 Standard iostream objects \[iostream.objects\]](iostream.objects.md) · prev: [29.4.2 Overview \[iostream.objects.overview\]](iostream.objects.overview.md) · next: [29.4.4 Wide stream objects \[wide.stream.objects\]](wide.stream.objects.md)

```cpp
istream cin;
```

**1** The object `cin` controls input from a stream buffer associated with the object `stdin`, declared in `<cstdio>` ([[cstdio.syn]](cstdio.syn.md)).

**2** After the object `cin` is initialized, `cin.tie()` returns `&cout`. Its state is otherwise the same as required for `basic_ios<char>::init` ([[basic.ios.cons]](basic.ios.cons.md)).

```cpp
ostream cout;
```

**3** The object `cout` controls output to a stream buffer associated with the object `stdout`, declared in `<cstdio>` ([[cstdio.syn]](cstdio.syn.md)).

```cpp
ostream cerr;
```

**4** The object `cerr` controls output to a stream buffer associated with the object `stderr`, declared in `<cstdio>` ([[cstdio.syn]](cstdio.syn.md)).

**5** After the object `cerr` is initialized, `cerr.flags() & unitbuf` is nonzero and `cerr.tie()` returns `&cout`. Its state is otherwise the same as required for `basic_ios<char>::init` ([[basic.ios.cons]](basic.ios.cons.md)).

```cpp
ostream clog;
```

**6** The object `clog` controls output to a stream buffer associated with the object `stderr`, declared in `<cstdio>` ([[cstdio.syn]](cstdio.syn.md)).
