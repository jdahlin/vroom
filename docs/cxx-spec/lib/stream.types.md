# 29.5.2 Types [stream.types]

[Contents](../README.md) · up: [29.5 Iostreams base classes \[iostreams.base\]](iostreams.base.md) · prev: [29.5.1 Header `<ios>` synopsis \[ios.syn\]](ios.syn.md) · next: [29.5.3 Class `ios_base` \[ios.base\]](ios.base.md)

```cpp
using streamoff = implementation-defined;
```

**1** The type `streamoff` is a synonym for one of the signed basic integral types of sufficient size to represent the maximum possible file size for the operating system.[^1]

```cpp
using streamsize = implementation-defined;
```

**2** The type `streamsize` is a synonym for one of the signed basic integral types. It is used to represent the number of characters transferred in an I/O operation, or the size of I/O buffers.[^2]

[^1]: Typically `long long`.
[^2]: `streamsize` is used in most places where ISO C would use `size_t`.
