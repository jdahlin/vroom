# C.5.3 [[expr]](expr.md): expressions [diff.expr]

[Contents](README.md) · up: [C.5 C++ and ISO C \[diff.iso\]](diff.iso.md) · prev: [C.5.2 [[basic]](basic.md): basics \[diff.basic\]](diff.basic.md) · next: [C.5.4 [[stmt.stmt]](stmt.stmt.md): statements \[diff.stat\]](diff.stat.md)

**1** **Affected subclause:** [[conv.ptr]](conv.ptr.md) <br>**Change:** Converting `void*` to a pointer-to-object type requires casting.

```cpp
char a[10];
void* b=a;
void foo() {
  char* c=b;
}
```

ISO C will accept this usage of pointer to void being assigned to a pointer to object type. C++ will not. <br>**Rationale:** C++ tries harder than C to enforce compile-time type safety. <br>**Effect on original feature:** Deletion of semantically well-defined feature. <br>**Difficulty of converting:** Could be automated. Violations will be diagnosed by the C++ translator. The fix is to add a cast. For example:

```cpp
char* c = (char*) b;
```

**How widely used:** This is fairly widely used but it is good programming practice to add the cast when assigning pointer-to-void to pointer-to-object. Some ISO C translators will give a warning if the cast is not used.

**2** **Affected subclause:** [[expr.call]](expr.call.md) <br>**Change:** Implicit declaration of functions is not allowed. <br>**Rationale:** The type-safe nature of C++. <br>**Effect on original feature:** Deletion of semantically well-defined feature. Note: the original feature was labeled as “obsolescent” in ISO C. <br>**Difficulty of converting:** Syntactic transformation. Facilities for producing explicit function declarations are fairly widespread commercially. <br>**How widely used:** Common.

**3** **Affected subclauses:** [[expr.post.incr]](expr.post.incr.md), [[expr.pre.incr]](expr.pre.incr.md) <br>**Change:** Decrement operator is not allowed with `bool` operand. <br>**Rationale:** Feature with surprising semantics. <br>**Effect on original feature:** A valid ISO C expression utilizing the decrement operator on a `bool` lvalue (for instance, via the C typedef in `<stdbool.h>` ([[depr.stdbool.h.syn]](depr.stdbool.h.syn.md))) is ill-formed in this International Standard.

**4** **Affected subclauses:** [[expr.sizeof]](expr.sizeof.md), [[expr.cast]](expr.cast.md) <br>**Change:** Types must be defined in declarations, not in expressions.<br> In C, a sizeof expression or cast expression may define a new type. For example,

```cpp
p = (void*)(struct x {int i;} *)0;
```

defines a new type, struct `x`. <br>**Rationale:** This prohibition helps to clarify the location of definitions in the source code. <br>**Effect on original feature:** Deletion of semantically well-defined feature. <br>**Difficulty of converting:** Syntactic transformation. <br>**How widely used:** Seldom.

**5** **Affected subclauses:** [[expr.cond]](expr.cond.md), [[expr.ass]](expr.ass.md), [[expr.comma]](expr.comma.md) <br>**Change:** The result of a conditional expression, an assignment expression, or a comma expression may be an lvalue. <br>**Rationale:** C++ is an object-oriented language, placing relatively more emphasis on lvalues. For example, function calls may yield lvalues. <br>**Effect on original feature:** Change to semantics of well-defined feature. Some C expressions that implicitly rely on lvalue-to-rvalue conversions will yield different results. For example,

```cpp
char arr[100];
sizeof(0, arr)
```

yields `100` in C++ and `sizeof(char*)` in C. <br>**Difficulty of converting:** Programs must add explicit casts to the appropriate rvalue. <br>**How widely used:** Rare.
