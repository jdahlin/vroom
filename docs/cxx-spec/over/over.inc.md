# 12.6.6 Increment and decrement [over.inc]

[Contents](../README.md) · up: [12.6 Overloaded operators \[over.oper\]](over.oper.md) · prev: [12.6.5 Class member access \[over.ref\]](over.ref.md) · next: [12.7 Built-in operators \[over.built\]](over.built.md)

**1** An *increment operator function* is a function named `operator++`. If this function is a non-static member function with no parameters, or a non-member function with one parameter, it defines the prefix increment operator `++` for objects of that type. If the function is a non-static member function with one parameter (which shall be of type `int`) or a non-member function with two parameters (the second of which shall be of type `int`), it defines the postfix increment operator `++` for objects of that type. When the postfix increment is called as a result of using the `++` operator, the `int` argument will have value zero.[^1] [*Example 1*:

```cpp
struct X {
  X&   operator++();            // prefix ++a
  X    operator++(int);         // postfix a++
};

struct Y { };
Y&   operator++(Y&);            // prefix ++b
Y    operator++(Y&, int);       // postfix b++

void f(X a, Y b) {
  ++a;                          // a.operator++();
  a++;                          // a.operator++(0);
  ++b;                          // operator++(b);
  b++;                          // operator++(b, 0);

  a.operator++();               // explicit call: like ++a;
  a.operator++(0);              // explicit call: like a++;
  operator++(b);                // explicit call: like ++b;
  operator++(b, 0);             // explicit call: like b++;
}
```

— *end example*]

**2** A *decrement operator function* is a function named `operator--` and is handled analogously to an increment operator function.

[^1]: Calling `operator++` explicitly, as in expressions like `a.operator++(2)`, has no special properties: The argument to `operator++` is `2`.
