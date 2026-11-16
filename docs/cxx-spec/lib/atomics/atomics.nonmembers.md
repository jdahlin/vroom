# 31.9 Non-member functions [atomics.nonmembers]

[Contents](../../README.md) · up: [31 Atomic operations library \[atomics\]](atomics.md) · prev: [31.8.6.2 Partial specialization for `weak_ptr` \[util.smartptr.atomic.weak\]](util.smartptr.atomic.weak.md) · next: [31.10 Flag type and operations \[atomics.flag\]](atomics.flag.md)

**1** A non-member function template whose name matches the pattern `atomic_f` or the pattern `atomic_f_explicit` invokes the member function `f`, with the value of the first parameter as the object expression and the values of the remaining parameters (if any) as the arguments of the member function call, in order. An argument for a parameter of type `atomic<T>::value_type*` is dereferenced when passed to the member function call. If no such member function exists, the program is ill-formed.

**2** [*Note 1*: The non-member functions enable programmers to write code that can be compiled as either C or C++, for example in a shared header file. — *end note*]
