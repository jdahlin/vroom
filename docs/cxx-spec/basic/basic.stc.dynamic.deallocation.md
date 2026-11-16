# 6.7.5.4.2 Deallocation functions [basic.stc.dynamic.deallocation]

[Contents](../README.md) · up: [6.7.5.4 Dynamic storage duration \[basic.stc.dynamic\]](basic.stc.dynamic.md) · prev: [6.7.5.4.1 Allocation functions \[basic.stc.dynamic.allocation\]](basic.stc.dynamic.allocation.md) · next: [6.7.5.4.3 Safely-derived pointers \[basic.stc.dynamic.safety\]](basic.stc.dynamic.safety.md)

**1** Deallocation functions shall be class member functions or global functions; a program is ill-formed if deallocation functions are declared in a namespace scope other than global scope or declared static in global scope.

**2** A deallocation function is a *destroying operator delete* if it has at least two parameters and its second parameter is of type `std::destroying_delete_t`. A destroying operator delete shall be a class member function named `operator delete`. [*Note 1*: Array deletion cannot use a destroying operator delete. — *end note*]

**3** Each deallocation function shall return `void`. If the function is a destroying operator delete declared in class type `C`, the type of its first parameter shall be `C*`; otherwise, the type of its first parameter shall be `void*`. A deallocation function may have more than one parameter. A *usual deallocation function* is a deallocation function whose parameters after the first are

- optionally, a parameter of type `std::destroying_delete_t`, then
- optionally, a parameter of type `std::size_t`[^1], then
- optionally, a parameter of type `std::align_val_t`.

A destroying operator delete shall be a usual deallocation function. A deallocation function may be an instance of a function template. Neither the first parameter nor the return type shall depend on a template parameter. A deallocation function template shall have two or more function parameters. A template instance is never a usual deallocation function, regardless of its signature.

**4** If a deallocation function terminates by throwing an exception, the behavior is undefined. The value of the first argument supplied to a deallocation function may be a null pointer value; if so, and if the deallocation function is one supplied in the standard library, the call has no effect.

**5** If the argument given to a deallocation function in the standard library is a pointer that is not the null pointer value ([[basic.compound]](basic.compound.md)), the deallocation function shall deallocate the storage referenced by the pointer, ending the duration of the region of storage.

[^1]: The global `operator delete(void*, std::size_t)` precludes use of an allocation function `void operator new(std::size_t, std::size_t)` as a placement allocation function ([[diff.cpp11.basic]](../diff/diff.cpp11.basic.md)).
