# 7.6.2.3 Await [expr.await]

[Contents](README.md) · up: [7.6.2 Unary expressions \[expr.unary\]](expr.unary.md) · prev: [7.6.2.2 Increment and decrement \[expr.pre.incr\]](expr.pre.incr.md) · next: [7.6.2.4 Sizeof \[expr.sizeof\]](expr.sizeof.md)

**1** The `co_await` expression is used to suspend evaluation of a coroutine ([[dcl.fct.def.coroutine]](dcl.fct.def.coroutine.md)) while awaiting completion of the computation represented by the operand expression.

```bnf
await-expression:
    'co_await' cast-expression
```

**2** An *await-expression* shall appear only in a potentially-evaluated expression within the *compound-statement* of a *function-body* outside of a *handler* ([[except.pre]](except.pre.md)). In a *declaration-statement* or in the *simple-declaration* (if any) of a *for-init-statement*, an *await-expression* shall appear only in an *initializer* of that *declaration-statement* or *simple-declaration*. An *await-expression* shall not appear in a default argument ([[dcl.fct.default]](dcl.fct.default.md)). An *await-expression* shall not appear in the initializer of a block-scope variable with static or thread storage duration. A context within a function where an *await-expression* can appear is called a *suspension context* of the function.

**3** Evaluation of an *await-expression* involves the following auxiliary types, expressions, and objects:

- *p* is an lvalue naming the promise object ([[dcl.fct.def.coroutine]](dcl.fct.def.coroutine.md)) of the enclosing coroutine and `P` is the type of that object.
- *a* is the *cast-expression* if the *await-expression* was implicitly produced by a *yield-expression* ([[expr.yield]](expr.yield.md)), an initial suspend point, or a final suspend point ([[dcl.fct.def.coroutine]](dcl.fct.def.coroutine.md)). Otherwise, the *unqualified-id* `await_transform` is looked up within the scope of `P` by class member access lookup ([[basic.lookup.classref]](basic.lookup.classref.md)), and if this lookup finds at least one declaration, then *a* is *p*`.await_transform(`*cast-expression*`)`; otherwise, *a* is the *cast-expression*.
- *o* is determined by enumerating the applicable `operator co_await` functions for an argument *a* ([[over.match.oper]](over.match.oper.md)), and choosing the best one through overload resolution ([[over.match]](over.match.md)). If overload resolution is ambiguous, the program is ill-formed. If no viable functions are found, *o* is *a*. Otherwise, *o* is a call to the selected function with the argument *a*. If *o* would be a prvalue, the temporary materialization conversion ([[conv.rval]](conv.rval.md)) is applied.
- *e* is an lvalue referring to the result of evaluating the (possibly-converted) *o*.
- *h* is an object of type `std::coroutine_handle<P>` referring to the enclosing coroutine.
- *await-ready* is the expression *e*`.await_ready()`, contextually converted to `bool`.
- *await-suspend* is the expression *e*`.await_suspend(`*h*`)`, which shall be a prvalue of type `void`, `bool`, or `std::coroutine_handle<Z>` for some type `Z`.
- *await-resume* is the expression *e*`.await_resume()`.

**4** The *await-expression* has the same type and value category as the *await-resume* expression.

**5** The *await-expression* evaluates the (possibly-converted) *o* expression and the *await-ready* expression, then:

- If the result of *await-ready* is `false`, the coroutine is considered suspended. Then:

  - If the type of *await-suspend* is `std::coroutine_handle<Z>`, *await-suspend*`.resume()` is evaluated. [*Note 1*: This resumes the coroutine referred to by the result of *await-suspend*. Any number of coroutines may be successively resumed in this fashion, eventually returning control flow to the current coroutine caller or resumer ([[dcl.fct.def.coroutine]](dcl.fct.def.coroutine.md)). — *end note*]
  - Otherwise, if the type of *await-suspend* is `bool`, *await-suspend* is evaluated, and the coroutine is resumed if the result is `false`.
  - Otherwise, *await-suspend* is evaluated.

  If the evaluation of *await-suspend* exits via an exception, the exception is caught, the coroutine is resumed, and the exception is immediately re-thrown ([[except.throw]](except.throw.md)). Otherwise, control flow returns to the current coroutine caller or resumer ([[dcl.fct.def.coroutine]](dcl.fct.def.coroutine.md)) without exiting any scopes ([[stmt.jump]](stmt.jump.md)).

- If the result of *await-ready* is `true`, or when the coroutine is resumed, the *await-resume* expression is evaluated, and its result is the result of the *await-expression*.

**6** [*Example 1*:

```cpp
template <typename T>
struct my_future {
  /* ... */
  bool await_ready();
  void await_suspend(std::coroutine_handle<>);
  T await_resume();
};

template <class Rep, class Period>
auto operator co_await(std::chrono::duration<Rep, Period> d) {
  struct awaiter {
    std::chrono::system_clock::duration duration;
    /* ... */
    awaiter(std::chrono::system_clock::duration d) : duration(d) {}
    bool await_ready() const { return duration.count() <= 0; }
    void await_resume() {}
    void await_suspend(std::coroutine_handle<> h) { /* ... */ }
  };
  return awaiter{d};
}

using namespace std::chrono;

my_future<int> h();

my_future<void> g() {
  std::cout << "just about go to sleep...\n";
  co_await 10ms;
  std::cout << "resumed\n";
  co_await h();
}

auto f(int x = co_await h());   // error: await-expression outside of function suspension context
int a[] = { co_await h() };     // error: await-expression outside of function suspension context
```

— *end example*]
