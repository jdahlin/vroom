# 13.4.1 Template type arguments [temp.arg.type]

[Contents](../README.md) · up: [13.4 Template arguments \[temp.arg\]](temp.arg.md) · prev: [13.4 Template arguments \[temp.arg\]](temp.arg.md) · next: [13.4.2 Template non-type arguments \[temp.arg.nontype\]](temp.arg.nontype.md)

**1** A *template-argument* for a *template-parameter* which is a type shall be a *type-id*.

**2** [*Example 1*:

```cpp
template <class T> class X { };
template <class T> void f(T t) { }
struct { } unnamed_obj;

void f() {
  struct A { };
  enum { e1 };
  typedef struct { } B;
  B b;
  X<A> x1;          // OK
  X<A*> x2;         // OK
  X<B> x3;          // OK
  f(e1);            // OK
  f(unnamed_obj);   // OK
  f(b);             // OK
}
```

— *end example*] [*Note 1*: A template type argument may be an incomplete type ([[basic.types]](../basic/basic.types.md)). — *end note*]
