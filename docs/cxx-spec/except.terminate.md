# 14.6.1 The `std::terminate` function [except.terminate]

[Contents](README.md) · up: [14.6 Special functions \[except.special\]](except.special.md) · prev: [14.6 Special functions \[except.special\]](except.special.md) · next: [14.6.2 The `std::uncaught_exceptions()` function \[except.uncaught\]](except.uncaught.md)

**1** In some situations exception handling must be abandoned for less subtle error handling techniques. [*Note 1*: These situations are:

- when the exception handling mechanism, after completing the initialization of the exception object but before activation of a handler for the exception ([[except.throw]](except.throw.md)), calls a function that exits via an exception, or
- when the exception handling mechanism cannot find a handler for a thrown exception ([[except.handle]](except.handle.md)), or
- when the search for a handler ([[except.handle]](except.handle.md)) encounters the outermost block of a function with a non-throwing exception specification ([[except.spec]](except.spec.md)), or
- when the destruction of an object during stack unwinding ([[except.ctor]](except.ctor.md)) terminates by throwing an exception, or
- when initialization of a non-local variable with static or thread storage duration ([[basic.start.dynamic]](basic.start.dynamic.md)) exits via an exception, or
- when destruction of an object with static or thread storage duration exits via an exception ([[basic.start.term]](basic.start.term.md)), or
- when execution of a function registered with `std::atexit` or `std::at_quick_exit` exits via an exception ([[support.start.term]](lib/support.start.term.md)), or
- when a *throw-expression* ([[expr.throw]](expr.throw.md)) with no operand attempts to rethrow an exception and no exception is being handled ([[except.throw]](except.throw.md)), or
- when the function `std::nested_exception::rethrow_nested` is called for an object that has captured no exception ([[except.nested]](lib/except.nested.md)), or
- when execution of the initial function of a thread exits via an exception ([[thread.thread.constr]](lib/thread.thread.constr.md)), or
- for a parallel algorithm whose `ExecutionPolicy` specifies such behavior ([[execpol.seq]](lib/execpol.seq.md), [[execpol.par]](lib/execpol.par.md), [[execpol.parunseq]](lib/execpol.parunseq.md)), when execution of an element access function ([[algorithms.parallel.defns]](lib/algorithms.parallel.defns.md)) of the parallel algorithm exits via an exception ([[algorithms.parallel.exceptions]](lib/algorithms.parallel.exceptions.md)), or
- when the destructor or the move assignment operator is invoked on an object of type `std::thread` that refers to a joinable thread ([[thread.thread.destr]](lib/thread.thread.destr.md), [[thread.thread.assign]](lib/thread.thread.assign.md)), or
- when a call to a `wait()`, `wait_until()`, or `wait_for()` function on a condition variable ([[thread.condition.condvar]](lib/thread.condition.condvar.md), [[thread.condition.condvarany]](lib/thread.condition.condvarany.md)) fails to meet a postcondition.

— *end note*]

**2** In such cases, the function `std::terminate` is called ([[exception.terminate]](lib/exception.terminate.md)). In the situation where no matching handler is found, it is implementation-defined whether or not the stack is unwound before `std::terminate` is called. In the situation where the search for a handler ([[except.handle]](except.handle.md)) encounters the outermost block of a function with a non-throwing exception specification ([[except.spec]](except.spec.md)), it is implementation-defined whether the stack is unwound, unwound partially, or not unwound at all before the function `std::terminate` is called. In all other situations, the stack shall not be unwound before the function `std::terminate` is called. An implementation is not permitted to finish stack unwinding prematurely based on a determination that the unwind process will eventually cause a call to the function `std::terminate`.
