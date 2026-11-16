# 13.7.6 Function templates [temp.fct]

[Contents](README.md) · up: [13.7 Template declarations \[temp.decls\]](temp.decls.md) · prev: [13.7.5.3 Members of class template specializations \[temp.class.spec.mfunc\]](temp.class.spec.mfunc.md) · next: [13.7.6.1 Function template overloading \[temp.over.link\]](temp.over.link.md)

**1** A function template defines an unbounded set of related functions. [*Example 1*: A family of sort functions might be declared like this:

```cpp
template<class T> class Array { };
template<class T> void sort(Array<T>&);
```

— *end example*]

**2** A function template can be overloaded with other function templates and with non-template functions ([[dcl.fct]](dcl.fct.md)). A non-template function is not related to a function template (i.e., it is never considered to be a specialization), even if it has the same name and type as a potentially generated function template specialization.[^1]

## Subclauses

- [13.7.6.1 Function template overloading \[temp.over.link\]](temp.over.link.md)
- [13.7.6.2 Partial ordering of function templates \[temp.func.order\]](temp.func.order.md)

[^1]: That is, declarations of non-template functions do not merely guide overload resolution of function template specializations with the same name. If such a non-template function is odr-used ([[basic.def.odr]](basic.def.odr.md)) in a program, it must be defined; it will not be implicitly instantiated using the function template definition.
