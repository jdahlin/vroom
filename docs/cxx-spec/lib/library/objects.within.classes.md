# 16.4.2.4 Private members [objects.within.classes]

[Contents](../../README.md) · up: [16.4.2 Other conventions \[conventions\]](conventions.md) · prev: [16.4.2.3 Functions within classes \[functions.within.classes\]](functions.within.classes.md) · next: [16.5 Library-wide requirements \[requirements\]](requirements.md)

**1** [[support]](../support/support.md) through [[thread]](../thread/thread.md) and [[depr]](../../depr/depr.md) do not specify the representation of classes, and intentionally omit specification of class members ([[class.mem]](../../class/class.mem.md)). An implementation may define static or non-static class members, or both, as needed to implement the semantics of the member functions specified in [[support]](../support/support.md) through [[thread]](../thread/thread.md) and [[depr]](../../depr/depr.md).

**2** For the sake of exposition, some subclauses provide representative declarations, and semantic requirements, for private members of classes that meet the external specifications of the classes. The declarations for such members are followed by a comment that ends with *exposition only*, as in:

```cpp
streambuf* sb;      // exposition only
```

**3** An implementation may use any technique that provides equivalent observable behavior.
