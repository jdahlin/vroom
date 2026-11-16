# 6.9.1 Sequential execution [intro.execution]

[Contents](README.md) · up: [6.9 Program execution \[basic.exec\]](basic.exec.md) · prev: [6.9 Program execution \[basic.exec\]](basic.exec.md) · next: [6.9.2 Multi-threaded executions and data races \[intro.multithread\]](intro.multithread.md)

**1** An instance of each object with automatic storage duration ([[basic.stc.auto]](basic.stc.auto.md)) is associated with each entry into its block. Such an object exists and retains its last-stored value during the execution of the block and while the block is suspended (by a call of a function, suspension of a coroutine ([[expr.await]](expr.await.md)), or receipt of a signal).

**2** A *constituent expression* is defined as follows:

- The constituent expression of an expression is that expression.
- The constituent expressions of a *braced-init-list* or of a (possibly parenthesized) *expression-list* are the constituent expressions of the elements of the respective list.
- The constituent expressions of a *brace-or-equal-initializer* of the form `=` *initializer-clause* are the constituent expressions of the *initializer-clause*.

[*Example 1*:

```cpp
struct A { int x; };
struct B { int y; struct A a; };
B b = { 5, { 1+1 } };
```

The constituent expressions of the *initializer* used for the initialization of `b` are `5` and `1+1`. — *end example*]

**3** The *immediate subexpressions* of an expression *E* are

- the constituent expressions of *E*’s operands ([[expr.prop]](expr.prop.md)),
- any function call that *E* implicitly invokes,
- if *E* is a *lambda-expression* ([[expr.prim.lambda]](expr.prim.lambda.md)), the initialization of the entities captured by copy and the constituent expressions of the *initializer* of the *init-capture*s,
- if *E* is a function call ([[expr.call]](expr.call.md)) or implicitly invokes a function, the constituent expressions of each default argument ([[dcl.fct.default]](dcl.fct.default.md)) used in the call, or
- if *E* creates an aggregate object ([[dcl.init.aggr]](dcl.init.aggr.md)), the constituent expressions of each default member initializer ([[class.mem]](class.mem.md)) used in the initialization.

**4** A *subexpression* of an expression *E* is an immediate subexpression of *E* or a subexpression of an immediate subexpression of *E*. [*Note 1*: Expressions appearing in the *compound-statement* of a *lambda-expression* are not subexpressions of the *lambda-expression*. — *end note*]

**5** A *full-expression* is

- an unevaluated operand ([[expr.prop]](expr.prop.md)),
- a *constant-expression* ([[expr.const]](expr.const.md)),
- an immediate invocation ([[expr.const]](expr.const.md)),
- an *init-declarator* ([[dcl.decl]](dcl.decl.md)) or a *mem-initializer* ([[class.base.init]](class.base.init.md)), including the constituent expressions of the initializer,
- an invocation of a destructor generated at the end of the lifetime of an object other than a temporary object ([[class.temporary]](class.temporary.md)) whose lifetime has not been extended, or
- an expression that is not a subexpression of another expression and that is not otherwise part of a full-expression.

If a language construct is defined to produce an implicit call of a function, a use of the language construct is considered to be an expression for the purposes of this definition. Conversions applied to the result of an expression in order to satisfy the requirements of the language construct in which the expression appears are also considered to be part of the full-expression. For an initializer, performing the initialization of the entity (including evaluating default member initializers of an aggregate) is also considered part of the full-expression. [*Example 2*:

```cpp
struct S {
  S(int i): I(i) { }            // full-expression is initialization of I
  int& v() { return I; }
  ~S() noexcept(false) { }
private:
  int I;
};

S s1(1);                        // full-expression comprises call of S::S(int)
void f() {
  S s2 = 2;                     // full-expression comprises call of S::S(int)
  if (S(3).v())                 // full-expression includes lvalue-to-rvalue and int to bool conversions,
                                // performed before temporary is deleted at end of full-expression
  { }
  bool b = noexcept(S());       // exception specification of destructor of S considered for noexcept

  // full-expression is destruction of s2 at end of block
}
struct B {
  B(S = S(0));
};
B b[2] = { B(), B() };          // full-expression is the entire initialization
                                // including the destruction of temporaries
```

— *end example*]

**6** [*Note 2*: The evaluation of a full-expression can include the evaluation of subexpressions that are not lexically part of the full-expression. For example, subexpressions involved in evaluating default arguments ([[dcl.fct.default]](dcl.fct.default.md)) are considered to be created in the expression that calls the function, not the expression that defines the default argument. — *end note*]

**7** Reading an object designated by a `volatile` glvalue ([[basic.lval]](basic.lval.md)), modifying an object, calling a library I/O function, or calling a function that does any of those operations are all *side effects*, which are changes in the state of the execution environment. *Evaluation* of an expression (or a subexpression) in general includes both value computations (including determining the identity of an object for glvalue evaluation and fetching a value previously assigned to an object for prvalue evaluation) and initiation of side effects. When a call to a library I/O function returns or an access through a volatile glvalue is evaluated the side effect is considered complete, even though some external actions implied by the call (such as the I/O itself) or by the `volatile` access may not have completed yet.

**8** *Sequenced before* is an asymmetric, transitive, pair-wise relation between evaluations executed by a single thread ([[intro.multithread]](intro.multithread.md)), which induces a partial order among those evaluations. Given any two evaluations *A* and *B*, if *A* is sequenced before *B* (or, equivalently, *B* is *sequenced after* *A*), then the execution of *A* shall precede the execution of *B*. If *A* is not sequenced before *B* and *B* is not sequenced before *A*, then *A* and *B* are *unsequenced*. [*Note 3*: The execution of unsequenced evaluations can overlap. — *end note*] Evaluations *A* and *B* are *indeterminately sequenced* when either *A* is sequenced before *B* or *B* is sequenced before *A*, but it is unspecified which. [*Note 4*: Indeterminately sequenced evaluations cannot overlap, but either could be executed first. — *end note*] An expression *X* is said to be sequenced before an expression *Y* if every value computation and every side effect associated with the expression *X* is sequenced before every value computation and every side effect associated with the expression *Y*.

**9** Every value computation and side effect associated with a full-expression is sequenced before every value computation and side effect associated with the next full-expression to be evaluated.[^1]

**10** Except where noted, evaluations of operands of individual operators and of subexpressions of individual expressions are unsequenced. [*Note 5*: In an expression that is evaluated more than once during the execution of a program, unsequenced and indeterminately sequenced evaluations of its subexpressions need not be performed consistently in different evaluations. — *end note*] The value computations of the operands of an operator are sequenced before the value computation of the result of the operator. If a side effect on a memory location ([[intro.memory]](intro.memory.md)) is unsequenced relative to either another side effect on the same memory location or a value computation using the value of any object in the same memory location, and they are not potentially concurrent ([[intro.multithread]](intro.multithread.md)), the behavior is undefined. [*Note 6*: The next subclause imposes similar, but more complex restrictions on potentially concurrent computations. — *end note*]

[*Example 3*:

```cpp
void g(int i) {
  i = 7, i++, i++;              // i becomes 9

  i = i++ + 1;                  // the value of i is incremented
  i = i++ + i;                  // undefined behavior
  i = i + 1;                    // the value of i is incremented
}
```

— *end example*]

**11** When calling a function (whether or not the function is inline), every value computation and side effect associated with any argument expression, or with the postfix expression designating the called function, is sequenced before execution of every expression or statement in the body of the called function. For each function invocation *F*, for every evaluation *A* that occurs within *F* and every evaluation *B* that does not occur within *F* but is evaluated on the same thread and as part of the same signal handler (if any), either *A* is sequenced before *B* or *B* is sequenced before *A*.[^2] [*Note 7*: If *A* and *B* would not otherwise be sequenced then they are indeterminately sequenced. — *end note*] Several contexts in C++ cause evaluation of a function call, even though no corresponding function call syntax appears in the translation unit. [*Example 4*: Evaluation of a *new-expression* invokes one or more allocation and constructor functions; see [[expr.new]](expr.new.md). For another example, invocation of a conversion function ([[class.conv.fct]](class.conv.fct.md)) can arise in contexts in which no function call syntax appears. — *end example*] The sequencing constraints on the execution of the called function (as described above) are features of the function calls as evaluated, whatever the syntax of the expression that calls the function might be.

**12** If a signal handler is executed as a result of a call to the `std::raise` function, then the execution of the handler is sequenced after the invocation of the `std::raise` function and before its return. [*Note 8*: When a signal is received for another reason, the execution of the signal handler is usually unsequenced with respect to the rest of the program. — *end note*]

[^1]: As specified in [[class.temporary]](class.temporary.md), after a full-expression is evaluated, a sequence of zero or more invocations of destructor functions for temporary objects takes place, usually in reverse order of the construction of each temporary object.
[^2]: In other words, function executions do not interleave with each other.
