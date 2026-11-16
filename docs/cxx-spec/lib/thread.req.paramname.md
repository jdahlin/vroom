# 32.2.1 Template parameter names [thread.req.paramname]

[Contents](../README.md) · up: [32.2 Requirements \[thread.req\]](thread.req.md) · prev: [32.2 Requirements \[thread.req\]](thread.req.md) · next: [32.2.2 Exceptions \[thread.req.exception\]](thread.req.exception.md)

**1** Throughout this Clause, the names of template parameters are used to express type requirements. If a template parameter is named `Predicate`, `operator()` applied to the template argument shall return a value that is convertible to `bool`. If a template parameter is named `Clock`, the corresponding template argument shall be a type `C` for which `is_clock_v<C>` is `true`; otherwise the program is ill-formed.
