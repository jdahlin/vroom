# 12.7 Built-in operators [over.built]

[Contents](../README.md) · up: [12 Overloading \[over\]](over.md) · prev: [12.6.6 Increment and decrement \[over.inc\]](over.inc.md) · next: [12.8 User-defined literals \[over.literal\]](over.literal.md)

**1** The candidate operator functions that represent the built-in operators defined in [[expr.compound]](../expr/expr.compound.md) are specified in this subclause. These candidate functions participate in the operator overload resolution process as described in [[over.match.oper]](over.match.oper.md) and are used for no other purpose. [*Note 1*: Because built-in operators take only operands with non-class type, and operator overload resolution occurs only when an operand expression originally has class or enumeration type, operator overload resolution can resolve to a built-in operator only when an operand has a class type that has a user-defined conversion to a non-class type appropriate for the operator, or when an operand has an enumeration type that can be converted to a type appropriate for the operator. Also note that some of the candidate operator functions given in this subclause are more permissive than the built-in operators themselves. As described in [[over.match.oper]](over.match.oper.md), after a built-in operator is selected by overload resolution the expression is subject to the requirements for the built-in operator given in [[expr.compound]](../expr/expr.compound.md), and therefore to any additional semantic constraints given there. If there is a user-written candidate with the same name and parameter types as a built-in candidate operator function, the built-in operator function is hidden and is not included in the set of candidate functions. — *end note*]

**2** In this subclause, the term *promoted integral type* is used to refer to those integral types which are preserved by integral promotion ([[conv.prom]](../expr/conv.prom.md)) (including e.g. `int` and `long` but excluding e.g. `char`). [*Note 2*: In all cases where a promoted integral type is required, an operand of unscoped enumeration type will be acceptable by way of the integral promotions. — *end note*]

**3** In the remainder of this subclause, *vq* represents either `volatile` or no cv-qualifier.

**4** For every pair (`T`, *vq*), where `T` is an arithmetic type other than `bool`, there exist candidate operator functions of the form

```cpp
vq T& operator++(vq T&);
T operator++(vq T&, int);
```

**5** For every pair (`T`, *vq*), where `T` is an arithmetic type other than `bool`, there exist candidate operator functions of the form

```cpp
vq T& operator--(vq T&);
T operator--(vq T&, int);
```

**6** For every pair (`T`, *vq*), where `T` is a cv-qualified or cv-unqualified object type, there exist candidate operator functions of the form

```cpp
T*vq& operator++(T*vq&);
T*vq& operator--(T*vq&);
T*    operator++(T*vq&, int);
T*    operator--(T*vq&, int);
```

**7** For every cv-qualified or cv-unqualified object type `T`, there exist candidate operator functions of the form

```cpp
T&    operator*(T*);
```

**8** For every function type `T` that does not have cv-qualifiers or a *ref-qualifier*, there exist candidate operator functions of the form

```cpp
T&    operator*(T*);
```

**9** For every type `T` there exist candidate operator functions of the form

```cpp
T*    operator+(T*);
```

**10** For every floating-point or promoted integral type `T`, there exist candidate operator functions of the form

```cpp
T operator+(T);
T operator-(T);
```

**11** For every promoted integral type `T`, there exist candidate operator functions of the form

```cpp
T operator~(T);
```

**12** For every quintuple (`C1`, `C2`, `T`, *cv1*, *cv2*), where `C2` is a class type, `C1` is the same type as `C2` or is a derived class of `C2`, and `T` is an object type or a function type, there exist candidate operator functions of the form

```cpp
cv12 T& operator->*(cv1 C1*, cv2 T C2::*);
```

where *cv12* is the union of *cv1* and *cv2*. The return type is shown for exposition only; see [[expr.mptr.oper]](../expr/expr.mptr.oper.md) for the determination of the operator’s result type.

**13** For every pair of types `L` and `R`, where each of `L` and `R` is a floating-point or promoted integral type, there exist candidate operator functions of the form

```cpp
LR      operator*(L, R);
LR      operator/(L, R);
LR      operator+(L, R);
LR      operator-(L, R);
bool    operator==(L, R);
bool    operator!=(L, R);
bool    operator<(L, R);
bool    operator>(L, R);
bool    operator<=(L, R);
bool    operator>=(L, R);
```

where `LR` is the result of the usual arithmetic conversions ([[expr.arith.conv]](../expr/expr.arith.conv.md)) between types `L` and `R`.

**14** For every integral type `T` there exists a candidate operator function of the form

```cpp
std::strong_ordering operator<=>(T, T);
```

**15** For every pair of floating-point types `L` and `R`, there exists a candidate operator function of the form

```cpp
std::partial_ordering operator<=>(L, R);
```

**16** For every cv-qualified or cv-unqualified object type `T` there exist candidate operator functions of the form

```cpp
T*      operator+(T*, std::ptrdiff_t);
T&      operator[](T*, std::ptrdiff_t);
T*      operator-(T*, std::ptrdiff_t);
T*      operator+(std::ptrdiff_t, T*);
T&      operator[](std::ptrdiff_t, T*);
```

**17** For every `T`, where `T` is a pointer to object type, there exist candidate operator functions of the form

```cpp
std::ptrdiff_t   operator-(T, T);
```

**18** For every `T`, where `T` is an enumeration type or a pointer type, there exist candidate operator functions of the form

```cpp
bool    operator==(T, T);
bool    operator!=(T, T);
bool    operator<(T, T);
bool    operator>(T, T);
bool    operator<=(T, T);
bool    operator>=(T, T);
R       operator<=>(T, T);
```

where `R` is the result type specified in [[expr.spaceship]](../expr/expr.spaceship.md).

**19** For every `T`, where `T` is a pointer-to-member type or `std::nullptr_t`, there exist candidate operator functions of the form

```cpp
bool operator==(T, T);
bool operator!=(T, T);
```

**20** For every pair of promoted integral types `L` and `R`, there exist candidate operator functions of the form

```cpp
LR      operator%(L, R);
LR      operator&(L, R);
LR      operator^(L, R);
LR      operator|(L, R);
L       operator<<(L, R);
L       operator>>(L, R);
```

where `LR` is the result of the usual arithmetic conversions ([[expr.arith.conv]](../expr/expr.arith.conv.md)) between types `L` and `R`.

**21** For every triple (`L`, *vq*, `R`), where `L` is an arithmetic type, and `R` is a floating-point or promoted integral type, there exist candidate operator functions of the form

```cpp
vq L&   operator=(vq L&, R);
vq L&   operator*=(vq L&, R);
vq L&   operator/=(vq L&, R);
vq L&   operator+=(vq L&, R);
vq L&   operator-=(vq L&, R);
```

**22** For every pair (`T`, *vq*), where `T` is any type, there exist candidate operator functions of the form

```cpp
T*vq&   operator=(T*vq&, T*);
```

**23** For every pair (`T`, *vq*), where `T` is an enumeration or pointer-to-member type, there exist candidate operator functions of the form

```cpp
vq T&   operator=(vq T&, T);
```

**24** For every pair (`T`, *vq*), where `T` is a cv-qualified or cv-unqualified object type, there exist candidate operator functions of the form

```cpp
T*vq&   operator+=(T*vq&, std::ptrdiff_t);
T*vq&   operator-=(T*vq&, std::ptrdiff_t);
```

**25** For every triple (`L`, *vq*, `R`), where `L` is an integral type, and `R` is a promoted integral type, there exist candidate operator functions of the form

```cpp
vq L&   operator%=(vq L&, R);
vq L&   operator<<=(vq L&, R);
vq L&   operator>>=(vq L&, R);
vq L&   operator&=(vq L&, R);
vq L&   operator^=(vq L&, R);
vq L&   operator|=(vq L&, R);
```

**26** There also exist candidate operator functions of the form

```cpp
bool    operator!(bool);
bool    operator&&(bool, bool);
bool    operator||(bool, bool);
```

**27** For every pair of types `L` and `R`, where each of `L` and `R` is a floating-point or promoted integral type, there exist candidate operator functions of the form

```cpp
LR      operator?:(bool, L, R);
```

where `LR` is the result of the usual arithmetic conversions ([[expr.arith.conv]](../expr/expr.arith.conv.md)) between types `L` and `R`. [*Note 3*: As with all these descriptions of candidate functions, this declaration serves only to describe the built-in operator for purposes of overload resolution. The operator “`?:`” cannot be overloaded. — *end note*]

**28** For every type `T`, where `T` is a pointer, pointer-to-member, or scoped enumeration type, there exist candidate operator functions of the form

```cpp
T       operator?:(bool, T, T);
```
