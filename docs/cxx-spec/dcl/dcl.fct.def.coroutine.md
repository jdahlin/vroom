# 9.5.4 Coroutine definitions [dcl.fct.def.coroutine]

[Contents](../README.md) · up: [9.5 Function definitions \[dcl.fct.def\]](dcl.fct.def.md) · prev: [9.5.3 Deleted definitions \[dcl.fct.def.delete\]](dcl.fct.def.delete.md) · next: [9.6 Structured binding declarations \[dcl.struct.bind\]](dcl.struct.bind.md)

**1** A function is a *coroutine* if its *function-body* encloses a *coroutine-return-statement* ([[stmt.return.coroutine]](../stmt/stmt.return.coroutine.md)), an *await-expression* ([[expr.await]](../expr/expr.await.md)), or a *yield-expression* ([[expr.yield]](../expr/expr.yield.md)). The *parameter-declaration-clause* of the coroutine shall not terminate with an ellipsis that is not part of a *parameter-declaration*.

**2** [*Example 1*:

```cpp
task<int> f();

task<void> g1() {
  int i = co_await f();
  std::cout << "f() => " << i << std::endl;
}

template <typename... Args>
task<void> g2(Args&&...) {      // OK, ellipsis is a pack expansion
  int i = co_await f();
  std::cout << "f() => " << i << std::endl;
}

task<void> g3(int a, ...) {     // error: variable parameter list not allowed
  int i = co_await f();
  std::cout << "f() => " << i << std::endl;
}
```

— *end example*]

**3** The *promise type* of a coroutine is `std::coroutine_traits<R, P_1, ..., P_n>::promise_type`, where `R` is the return type of the function, and `P`<sub>1</sub> …`P`<sub>*n*</sub> are the sequence of types of the function parameters, preceded by the type of the implicit object parameter ([[over.match.funcs]](../over/over.match.funcs.md)) if the coroutine is a non-static member function. The promise type shall be a class type.

**4** In the following, `p`<sub>*i*</sub> is an lvalue of type `P`<sub>*i*</sub>, where `p`<sub>1</sub> denotes `*this` and `p`<sub>*i*+1</sub> denotes the *i*<sup></sup>*th* function parameter for a non-static member function, and `p`<sub>*i*</sub> denotes the *i*<sup></sup>*th* function parameter otherwise.

**5** A coroutine behaves as if its *function-body* were replaced by:

```bnf
'{'
    promise-type promise promise-constructor-arguments ';'
     'try' '{'
        'co_await' 'promise.initial_suspend()' ';'
        function-body
    '} catch ( ... ) {'
        'if (!initial-await-resume-called)'
            'throw' ';'
        'promise.unhandled_exception()' ';'
    '}'
final-suspend ':'
    'co_await' 'promise.final_suspend()' ';'
'}'
```

where

- the *await-expression* containing the call to `initial_suspend` is the *initial suspend point*, and
- the *await-expression* containing the call to `final_suspend` is the *final suspend point*, and
- *initial-await-resume-called* is initially `false` and is set to `true` immediately before the evaluation of the *await-resume* expression ([[expr.await]](../expr/expr.await.md)) of the initial suspend point, and
- *promise-type* denotes the promise type, and
- the object denoted by the exposition-only name `promise` is the *promise object* of the coroutine, and
- the label denoted by the name `final-suspend` is defined for exposition only ([[stmt.return.coroutine]](../stmt/stmt.return.coroutine.md)), and
- *promise-constructor-arguments* is determined as follows: overload resolution is performed on a promise constructor call created by assembling an argument list with lvalues `p`<sub>1</sub> …`p`<sub>*n*</sub>. If a viable constructor is found ([[over.match.viable]](../over/over.match.viable.md)), then *promise-constructor-arguments* is `(p_1, ..., p_n)`, otherwise *promise-constructor-arguments* is empty.

**6** The *unqualified-id*s `return_void` and `return_value` are looked up in the scope of the promise type. If both are found, the program is ill-formed. [*Note 1*: If the *unqualified-id* `return_void` is found, flowing off the end of a coroutine is equivalent to a `co_return` with no operand. Otherwise, flowing off the end of a coroutine results in undefined behavior ([[stmt.return.coroutine]](../stmt/stmt.return.coroutine.md)). — *end note*]

**7** The expression `promise.get_return_object()` is used to initialize the glvalue result or prvalue result object of a call to a coroutine. The call to `get_return_object` is sequenced before the call to `initial_suspend` and is invoked at most once.

**8** A suspended coroutine can be resumed to continue execution by invoking a resumption member function ([[coroutine.handle.resumption]](../lib/support/coroutine.handle.resumption.md)) of a coroutine handle ([[coroutine.handle]](../lib/support/coroutine.handle.md)) that refers to the coroutine. The function that invoked a resumption member function is called the *resumer*. Invoking a resumption member function for a coroutine that is not suspended results in undefined behavior.

**9** An implementation may need to allocate additional storage for a coroutine. This storage is known as the *coroutine state* and is obtained by calling a non-array allocation function ([[basic.stc.dynamic.allocation]](../basic/basic.stc.dynamic.allocation.md)). The allocation function’s name is looked up in the scope of the promise type. If this lookup fails, the allocation function’s name is looked up in the global scope. If the lookup finds an allocation function in the scope of the promise type, overload resolution is performed on a function call created by assembling an argument list. The first argument is the amount of space requested, and has type `std::size_t`. The lvalues `p`<sub>1</sub> …`p`<sub>*n*</sub> are the succeeding arguments. If no viable function is found ([[over.match.viable]](../over/over.match.viable.md)), overload resolution is performed again on a function call created by passing just the amount of space required as an argument of type `std::size_t`.

**10** The *unqualified-id* `get_return_object_on_allocation_failure` is looked up in the scope of the promise type by class member access lookup ([[basic.lookup.classref]](../basic/basic.lookup.classref.md)). If any declarations are found, then the result of a call to an allocation function used to obtain storage for the coroutine state is assumed to return `nullptr` if it fails to obtain storage, and if a global allocation function is selected, the `::operator new(size_t, nothrow_t)` form is used. The allocation function used in this case shall have a non-throwing *noexcept-specification*. If the allocation function returns `nullptr`, the coroutine returns control to the caller of the coroutine and the return value is obtained by a call to `T::get_return_object_on_allocation_failure()`, where `T` is the promise type.

[*Example 2*:

```cpp
#include <iostream>
#include <coroutine>

// ::operator new(size_t, nothrow_t) will be used if allocation is needed
struct generator {
  struct promise_type;
  using handle = std::coroutine_handle<promise_type>;
  struct promise_type {
    int current_value;
    static auto get_return_object_on_allocation_failure() { return generator{nullptr}; }
    auto get_return_object() { return generator{handle::from_promise(*this)}; }
    auto initial_suspend() { return std::suspend_always{}; }
    auto final_suspend() { return std::suspend_always{}; }
    void unhandled_exception() { std::terminate(); }
    void return_void() {}
    auto yield_value(int value) {
      current_value = value;
      return std::suspend_always{};
    }
  };
  bool move_next() { return coro ? (coro.resume(), !coro.done()) : false; }
  int current_value() { return coro.promise().current_value; }
  generator(generator const&) = delete;
  generator(generator && rhs) : coro(rhs.coro) { rhs.coro = nullptr; }
  ~generator() { if (coro) coro.destroy(); }
private:
  generator(handle h) : coro(h) {}
  handle coro;
};
generator f() { co_yield 1; co_yield 2; }
int main() {
  auto g = f();
  while (g.move_next()) std::cout << g.current_value() << std::endl;
}
```

— *end example*]

**11** The coroutine state is destroyed when control flows off the end of the coroutine or the `destroy` member function ([[coroutine.handle.resumption]](../lib/support/coroutine.handle.resumption.md)) of a coroutine handle ([[coroutine.handle]](../lib/support/coroutine.handle.md)) that refers to the coroutine is invoked. In the latter case objects with automatic storage duration that are in scope at the suspend point are destroyed in the reverse order of the construction. The storage for the coroutine state is released by calling a non-array deallocation function ([[basic.stc.dynamic.deallocation]](../basic/basic.stc.dynamic.deallocation.md)). If `destroy` is called for a coroutine that is not suspended, the program has undefined behavior.

**12** The deallocation function’s name is looked up in the scope of the promise type. If this lookup fails, the deallocation function’s name is looked up in the global scope. If deallocation function lookup finds both a usual deallocation function with only a pointer parameter and a usual deallocation function with both a pointer parameter and a size parameter, then the selected deallocation function shall be the one with two parameters. Otherwise, the selected deallocation function shall be the function with one parameter. If no usual deallocation function is found, the program is ill-formed. The selected deallocation function shall be called with the address of the block of storage to be reclaimed as its first argument. If a deallocation function with a parameter of type `std::size_t` is used, the size of the block is passed as the corresponding argument.

**13** When a coroutine is invoked, after initializing its parameters ([[expr.call]](../expr/expr.call.md)), a copy is created for each coroutine parameter. For a parameter of type *cv* `T`, the copy is a variable of type *cv* `T` with automatic storage duration that is direct-initialized from an xvalue of type `T` referring to the parameter. [*Note 2*: An original parameter object is never a const or volatile object ([[basic.type.qualifier]](../basic/basic.type.qualifier.md)). — *end note*] The initialization and destruction of each parameter copy occurs in the context of the called coroutine. Initializations of parameter copies are sequenced before the call to the coroutine promise constructor and indeterminately sequenced with respect to each other. The lifetime of parameter copies ends immediately after the lifetime of the coroutine promise object ends. [*Note 3*: If a coroutine has a parameter passed by reference, resuming the coroutine after the lifetime of the entity referred to by that parameter has ended is likely to result in undefined behavior. — *end note*]

**14** If the evaluation of the expression `promise.unhandled_exception()` exits via an exception, the coroutine is considered suspended at the final suspend point.

**15** The expression `co_await` `promise.final_suspend()` shall not be potentially-throwing ([[except.spec]](../except/except.spec.md)).
