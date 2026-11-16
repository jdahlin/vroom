# 11.6 Local class declarations [class.local]

[Contents](../README.md) · up: [11 Classes \[class\]](class.md) · prev: [11.5.1 Anonymous unions \[class.union.anon\]](class.union.anon.md) · next: [11.7 Derived classes \[class.derived\]](class.derived.md)

**1** A class can be declared within a function definition; such a class is called a *local class*. The name of a local class is local to its enclosing scope. The local class is in the scope of the enclosing scope, and has the same access to names outside the function as does the enclosing function. [*Note 1*: A declaration in a local class cannot odr-use ([[basic.def.odr]](../basic/basic.def.odr.md)) a local entity from an enclosing scope. — *end note*] [*Example 1*:

```cpp
int x;
void f() {
  static int s;
  int x;
  const int N = 5;
  extern int q();
  int arr[2];
  auto [y, z] = arr;

  struct local {
    int g() { return x; }       // error: odr-use of non-odr-usable variable x
    int h() { return s; }       // OK
    int k() { return ::x; }     // OK
    int l() { return q(); }     // OK
    int m() { return N; }       // OK: not an odr-use
    int* n() { return &N; }     // error: odr-use of non-odr-usable variable N
    int p() { return y; }       // error: odr-use of non-odr-usable structured binding y
  };
}

local* p = 0;                   // error: local not in scope
```

— *end example*]

**2** An enclosing function has no special access to members of the local class; it obeys the usual access rules ([[class.access]](class.access.md)). Member functions of a local class shall be defined within their class definition, if they are defined at all.

**3** If class `X` is a local class a nested class `Y` may be declared in class `X` and later defined in the definition of class `X` or be later defined in the same scope as the definition of class `X`. A class nested within a local class is a local class.

**4** [*Note 2*: A local class cannot have static data members ([[class.static.data]](class.static.data.md)). — *end note*]
