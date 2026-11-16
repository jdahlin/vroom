# 12.4.1.1.2 Call to object of class type [over.call.object]

[Contents](../README.md) · up: [12.4.1.1 Function call syntax \[over.match.call\]](over.match.call.md) · prev: [12.4.1.1.1 Call to named function \[over.call.func\]](over.call.func.md) · next: [12.4.1.2 Operators in expressions \[over.match.oper\]](over.match.oper.md)

**1** If the *postfix-expression* `E` in the function call syntax evaluates to a class object of type “*cv* `T`”, then the set of candidate functions includes at least the function call operators of `T`. The function call operators of `T` are obtained by ordinary lookup of the name `operator()` in the context of `(E).operator()`.

**2** In addition, for each non-explicit conversion function declared in `T` of the form

```bnf
'operator' conversion-type-id '()' cv-qualifier-seqₒₚₜ ref-qualifierₒₚₜ noexcept-specifierₒₚₜ attribute-specifier-seqₒₚₜ ';'
```

where the optional *cv-qualifier-seq* is the same cv-qualification as, or a greater cv-qualification than, *cv*, and where *conversion-type-id* denotes the type “pointer to function of (`P`<sub>1</sub>, …, `P`<sub>*n*</sub>) returning `R`”, or the type “reference to pointer to function of (`P`<sub>1</sub>, …, `P`<sub>*n*</sub>) returning `R`”, or the type “reference to function of (`P`<sub>1</sub>, …, `P`<sub>*n*</sub>) returning `R`”, a *surrogate call function* with the unique name *call-function* and having the form

```bnf
'R' call-function '(' conversion-type-id  'F, P_1 a_1, ..., P_n a_n)' '{ return F (a_1, ..., a_n); }'
```

is also considered as a candidate function. Similarly, surrogate call functions are added to the set of candidate functions for each non-explicit conversion function declared in a base class of `T` provided the function is not hidden within `T` by another intervening declaration.[^1]

**3** The argument list submitted to overload resolution consists of the argument expressions present in the function call syntax preceded by the implied object argument `(E)`. [*Note 1*: When comparing the call against the function call operators, the implied object argument is compared against the implicit object parameter of the function call operator. When comparing the call against a surrogate call function, the implied object argument is compared against the first parameter of the surrogate call function. The conversion function from which the surrogate call function was derived will be used in the conversion sequence for that parameter since it converts the implied object argument to the appropriate function pointer or reference required by that first parameter. — *end note*] [*Example 1*:

```cpp
int f1(int);
int f2(float);
typedef int (*fp1)(int);
typedef int (*fp2)(float);
struct A {
  operator fp1() { return f1; }
  operator fp2() { return f2; }
} a;
int i = a(1);                   // calls f1 via pointer returned from conversion function
```

— *end example*]

[^1]: Note that this construction can yield candidate call functions that cannot be differentiated one from the other by overload resolution because they have identical declarations or differ only in their return type. The call will be ambiguous if overload resolution cannot select a match to the call that is uniquely better than such undifferentiable functions.
