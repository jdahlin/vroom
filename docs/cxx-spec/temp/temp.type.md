# 13.6 Type equivalence [temp.type]

[Contents](../README.md) · up: [13 Templates \[temp\]](temp.md) · prev: [13.5.4 Partial ordering by constraints \[temp.constr.order\]](temp.constr.order.md) · next: [13.7 Template declarations \[temp.decls\]](temp.decls.md)

**1** Two *template-id*s are the same if

- their *template-name*s, *operator-function-id*s, or *literal-operator-id*s refer to the same template, and
- their corresponding type *template-argument*s are the same type, and
- their corresponding non-type *template-argument*s are template-argument-equivalent (see below) after conversion to the type of the *template-parameter*, and
- their corresponding template *template-argument*s refer to the same template.

Two *template-id*s that are the same refer to the same class, function, or variable.

**2** Two values are *template-argument-equivalent* if they are of the same type and

- they are of integral type and their values are the same, or
- they are of floating-point type and their values are identical, or
- they are of type `std::nullptr_t`, or
- they are of enumeration type and their values are the same,[^1] or
- they are of pointer type and they have the same pointer value, or
- they are of pointer-to-member type and they refer to the same class member or are both the null member pointer value, or
- they are of reference type and they refer to the same object or function, or
- they are of array type and their corresponding elements are template-argument-equivalent,[^2] or
- they are of union type and either they both have no active member or they have the same active member and their active members are template-argument-equivalent, or
- they are of class type and their corresponding direct subobjects and reference members are template-argument-equivalent.

**3** [*Example 1*:

```cpp
template<class E, int size> class buffer { /* ... */ };
buffer<char,2*512> x;
buffer<char,1024> y;
```

declares `x` and `y` to be of the same type, and

```cpp
template<class T, void(*err_fct)()> class list { /* ... */ };
list<int,&error_handler1> x1;
list<int,&error_handler2> x2;
list<int,&error_handler2> x3;
list<char,&error_handler2> x4;
```

declares `x2` and `x3` to be of the same type. Their type differs from the types of `x1` and `x4`.

```cpp
template<class T> struct X { };
template<class> struct Y { };
template<class T> using Z = Y<T>;
X<Y<int> > y;
X<Z<int> > z;
```

declares `y` and `z` to be of the same type. — *end example*]

**4** If an expression *e* is type-dependent ([[temp.dep.expr]](temp.dep.expr.md)), `decltype(e)` denotes a unique dependent type. Two such *decltype-specifier*s refer to the same type only if their *expression*s are equivalent ([[temp.over.link]](temp.over.link.md)). [*Note 1*: However, such a type may be aliased, e.g., by a *typedef-name*. — *end note*]

[^1]: The identity of enumerators is not preserved.
[^2]: An array as a *template-parameter* decays to a pointer.
