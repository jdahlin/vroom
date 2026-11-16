# C.1.6 [[over]](../over/over.md): overloading [diff.cpp17.over]

[Contents](../README.md) · up: [C.1 C++ and ISO C++ 2017 \[diff.cpp17\]](diff.cpp17.md) · prev: [C.1.5 [[class]](../class/class.md): classes \[diff.cpp17.class\]](diff.cpp17.class.md) · next: [C.1.7 [[temp]](../temp/temp.md): templates \[diff.cpp17.temp\]](diff.cpp17.temp.md)

**1** **Affected subclause:** [[over.match.oper]](../over/over.match.oper.md) <br>**Change:** Equality and inequality expressions can now find reversed and rewritten candidates. <br>**Rationale:** Improve consistency of equality with three-way comparison and make it easier to write the full complement of equality operations. <br>**Effect on original feature:** Equality and inequality expressions between two objects of different types, where one is convertible to the other, could invoke a different operator. Equality and inequality expressions between two objects of the same type could become ambiguous.

```cpp
struct A {
  operator int() const;
};

bool operator==(A, int);        // #1
// #2 is built-in candidate: bool operator==(int, int);
// #3 is built-in candidate: bool operator!=(int, int);

int check(A x, A y) {
  return (x == y) +             // ill-formed; previously well-formed
    (10 == x) +                 // calls #1, previously selected #2
    (10 != x);                  // calls #1, previously selected #3
}
```
