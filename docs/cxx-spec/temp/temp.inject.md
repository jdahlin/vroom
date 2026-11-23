# 13.8.5 Friend names declared within a class template [temp.inject]

[Contents](../README.md) · up: [13.8 Name resolution \[temp.res\]](temp.res.md) · prev: [13.8.4.2 Candidate functions \[temp.dep.candidate\]](temp.dep.candidate.md) · next: [13.9 Template instantiation and specialization \[temp.spec\]](temp.spec.md)

**1** Friend classes or functions can be declared within a class template. When a template is instantiated, the names of its friends are treated as if the specialization had been explicitly declared at its point of instantiation.

**2** As with non-template classes, the names of namespace-scope friend functions of a class template specialization are not visible during an ordinary lookup unless explicitly declared at namespace scope ([[class.friend]](../class/class.friend.md)). Such names may be found under the rules for associated classes ([[basic.lookup.argdep]](../basic/basic.lookup.argdep.md)).[^1] [*Example 1*:

```cpp
template<typename T> struct number {
  number(int);
  friend number gcd(number x, number y) { return 0; };
};

void g() {
  number<double> a(3), b(4);
  a = gcd(a,b);     // finds gcd because number<double> is an associated class,
                    // making gcd visible in its namespace (global scope)
  b = gcd(3,4);     // error: gcd is not visible
}
```

— *end example*]

[^1]: Friend declarations do not introduce new names into any scope, either when the template is declared or when it is instantiated.
