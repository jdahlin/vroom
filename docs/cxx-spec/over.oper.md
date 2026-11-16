# 12.6 Overloaded operators [over.oper]

[Contents](README.md) · up: [12 Overloading \[over\]](over.md) · prev: [12.5 Address of overloaded function \[over.over\]](over.over.md) · next: [12.6.1 Unary operators \[over.unary\]](over.unary.md)

**1** A function declaration having one of the following *operator-function-id*s as its name declares an *operator function*. A function template declaration having one of the following *operator-function-id*s as its name declares an *operator function template*. A specialization of an operator function template is also an operator function. An operator function is said to *implement* the operator named in its *operator-function-id*.

```bnf
operator-function-id:
    'operator' operator
```

```bnf
operator: one of
    'new delete new[] delete[] co_await () [] -> ->*'
    '~ ! + - * / % ^ &'
    '| = += -= *= /= %= ^= &='
    '|= == != < > <= >= <=> &&'
    '|| << >> <<= >>= ++ --,'
```

[*Note 1*: The operators `new[]`, `delete[]`, `()`, and `[]` are formed from more than one token. The latter two operators are function call ([[expr.call]](expr.call.md)) and subscripting ([[expr.sub]](expr.sub.md)). — *end note*]

**2** Both the unary and binary forms of

```bnf
'+ - * &'
```

can be overloaded.

**3** [*Note 2*: The following operators cannot be overloaded:

```bnf
'. .* :: ?:'
```

nor can the preprocessing symbols `#` ([[cpp.stringize]](cpp.stringize.md)) and `##` ([[cpp.concat]](cpp.concat.md)). — *end note*]

**4** Operator functions are usually not called directly; instead they are invoked to evaluate the operators they implement ([[over.unary]](over.unary.md) – [[over.inc]](over.inc.md)). They can be explicitly called, however, using the *operator-function-id* as the name of the function in the function call syntax ([[expr.call]](expr.call.md)). [*Example 1*:

```cpp
complex z = a.operator+(b);     // complex z = a+b;
void* p = operator new(sizeof(int)*n);
```

— *end example*]

**5** The allocation and deallocation functions, `operator` `new`, `operator` `new[]`, `operator` `delete`, and `operator` `delete[]`, are described completely in [[basic.stc.dynamic]](basic.stc.dynamic.md). The attributes and restrictions found in the rest of this subclause do not apply to them unless explicitly stated in [[basic.stc.dynamic]](basic.stc.dynamic.md).

**6** The `co_await` operator is described completely in [[expr.await]](expr.await.md). The attributes and restrictions found in the rest of this subclause do not apply to it unless explicitly stated in [[expr.await]](expr.await.md).

**7** An operator function shall either be a non-static member function or be a non-member function that has at least one parameter whose type is a class, a reference to a class, an enumeration, or a reference to an enumeration. It is not possible to change the precedence, grouping, or number of operands of operators. The meaning of the operators `=`, (unary) `&`, and `,` (comma), predefined for each type, can be changed for specific class types by defining operator functions that implement these operators. Likewise, the meaning of the operators (unary) `&` and `,` (comma) can be changed for specific enumeration types. Operator functions are inherited in the same manner as other base class functions.

**8** An operator function shall be a prefix unary, binary, function call, subscripting, class member access, increment, or decrement operator function.

**9** [*Note 3*: The identities among certain predefined operators applied to basic types (for example, `++a` ≡ `a+=1`) need not hold for operator functions. Some predefined operators, such as `+=`, require an operand to be an lvalue when applied to basic types; this is not required by operator functions. — *end note*]

**10** An operator function cannot have default arguments ([[dcl.fct.default]](dcl.fct.default.md)), except where explicitly stated below. Operator functions cannot have more or fewer parameters than the number required for the corresponding operator, as described in the rest of this subclause.

**11** Operators not mentioned explicitly in subclauses [[over.ass]](over.ass.md) through [[over.inc]](over.inc.md) act as ordinary unary and binary operators obeying the rules of [[over.unary]](over.unary.md) or [[over.binary]](over.binary.md).

## Subclauses

- [12.6.1 Unary operators \[over.unary\]](over.unary.md)
- [12.6.2 Binary operators \[over.binary\]](over.binary.md)
  - [12.6.2.1 Simple assignment \[over.ass\]](over.ass.md)
- [12.6.3 Function call \[over.call\]](over.call.md)
- [12.6.4 Subscripting \[over.sub\]](over.sub.md)
- [12.6.5 Class member access \[over.ref\]](over.ref.md)
- [12.6.6 Increment and decrement \[over.inc\]](over.inc.md)
