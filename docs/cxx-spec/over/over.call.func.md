# 12.4.1.1.1 Call to named function [over.call.func]

[Contents](../README.md) · up: [12.4.1.1 Function call syntax \[over.match.call\]](over.match.call.md) · prev: [12.4.1.1 Function call syntax \[over.match.call\]](over.match.call.md) · next: [12.4.1.1.2 Call to object of class type \[over.call.object\]](over.call.object.md)

**1** Of interest in [[over.call.func]](over.call.func.md) are only those function calls in which the *postfix-expression* ultimately contains a name that denotes one or more functions that might be called. Such a *postfix-expression*, perhaps nested arbitrarily deep in parentheses, has one of the following forms:

```bnf
postfix-expression:
    postfix-expression '.' id-expression
    postfix-expression '->' id-expression
    primary-expression
```

These represent two syntactic subcategories of function calls: qualified function calls and unqualified function calls.

**2** In qualified function calls, the name to be resolved is an *id-expression* and is preceded by an `->` or `.` operator. Since the construct `A->B` is generally equivalent to `(*A).B`, the rest of [[over]](over.md) assumes, without loss of generality, that all member function calls have been normalized to the form that uses an object and the `.` operator. Furthermore, [[over]](over.md) assumes that the *postfix-expression* that is the left operand of the `.` operator has type “*cv* `T`” where `T` denotes a class.[^1] Under this assumption, the *id-expression* in the call is looked up as a member function of `T` following the rules for looking up names in classes ([[class.member.lookup]](../class/class.member.lookup.md)). The function declarations found by that lookup constitute the set of candidate functions. The argument list is the *expression-list* in the call augmented by the addition of the left operand of the `.` operator in the normalized member function call as the implied object argument ([[over.match.funcs]](over.match.funcs.md)).

**3** In unqualified function calls, the name is not qualified by an `->` or `.` operator and has the more general form of a *primary-expression*. The name is looked up in the context of the function call following the normal rules for name lookup in expressions ([[basic.lookup]](../basic/basic.lookup.md)). The function declarations found by that lookup constitute the set of candidate functions. Because of the rules for name lookup, the set of candidate functions consists (1) entirely of non-member functions or (2) entirely of member functions of some class `T`. In case (1), the argument list is the same as the *expression-list* in the call. In case (2), the argument list is the *expression-list* in the call augmented by the addition of an implied object argument as in a qualified function call. If the keyword `this` ([[class.this]](../class/class.this.md)) is in scope and refers to class `T`, or a derived class of `T`, then the implied object argument is `(*this)`. If the keyword `this` is not in scope or refers to another class, then a contrived object of type `T` becomes the implied object argument.[^2] If the argument list is augmented by a contrived object and overload resolution selects one of the non-static member functions of `T`, the call is ill-formed.

[^1]: Note that cv-qualifiers on the type of objects are significant in overload resolution for both glvalue and class prvalue objects.
[^2]: An implied object argument must be contrived to correspond to the implicit object parameter attributed to member functions during overload resolution. It is not used in the call to the selected function. Since the member functions all have the same implicit object parameter, the contrived object will not be the cause to select or reject a function.
