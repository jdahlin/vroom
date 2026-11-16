# 12.1 Preamble [over.pre]

[Contents](README.md) · up: [12 Overloading \[over\]](over.md) · prev: [12 Overloading \[over\]](over.md) · next: [12.2 Overloadable declarations \[over.load\]](over.load.md)

**1** When two or more different declarations are specified for a single name in the same scope, that name is said to be *overloaded*, and the declarations are called *overloaded declarations*. Only function and function template declarations can be overloaded; variable and type declarations cannot be overloaded.

**2** When a function name is used in a call, which function declaration is being referenced and the validity of the call are determined by comparing the types of the arguments at the point of use with the types of the parameters in the declarations that are visible at the point of use. This function selection process is called *overload resolution* and is defined in [[over.match]](over.match.md). [*Example 1*:

```cpp
double abs(double);
int abs(int);

abs(1);             // calls abs(int);
abs(1.0);           // calls abs(double);
```

— *end example*]
