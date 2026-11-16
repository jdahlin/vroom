# 7.6.18 Throwing an exception [expr.throw]

[Contents](../README.md) · up: [7.6 Compound expressions \[expr.compound\]](expr.compound.md) · prev: [7.6.17 Yielding a value \[expr.yield\]](expr.yield.md) · next: [7.6.19 Assignment and compound assignment operators \[expr.ass\]](expr.ass.md)

```bnf
throw-expression:
    'throw'  assignment-expressionₒₚₜ
```

**1** A *throw-expression* is of type `void`.

**2** Evaluating a *throw-expression* with an operand throws an exception ([[except.throw]](../except/except.throw.md)); the type of the exception object is determined by removing any top-level *cv-qualifier*s from the static type of the operand and adjusting the type from “array of `T`” or function type `T` to “pointer to `T`”.

**3** A *throw-expression* with no operand rethrows the currently handled exception ([[except.handle]](../except/except.handle.md)). The exception is reactivated with the existing exception object; no new exception object is created. The exception is no longer considered to be caught. [*Example 1*: Code that must be executed because of an exception, but cannot completely handle the exception itself, can be written like this:

```cpp
try {
  // ...
} catch (...) {     // catch all exceptions
  // respond (partially) to exception
  throw;            // pass the exception to some other handler
}
```

— *end example*]

**4** If no exception is presently being handled, evaluating a *throw-expression* with no operand calls `std::terminate()` ([[except.terminate]](../except/except.terminate.md)).
