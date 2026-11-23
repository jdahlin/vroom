# 13.10.2.4 Deducing template arguments during partial ordering [temp.deduct.partial]

[Contents](../README.md) · up: [13.10.2 Template argument deduction \[temp.deduct\]](temp.deduct.md) · prev: [13.10.2.3 Deducing conversion function template arguments \[temp.deduct.conv\]](temp.deduct.conv.md) · next: [13.10.2.5 Deducing template arguments from a type \[temp.deduct.type\]](temp.deduct.type.md)

**1** Template argument deduction is done by comparing certain types associated with the two function templates being compared.

**2** Two sets of types are used to determine the partial ordering. For each of the templates involved there is the original function type and the transformed function type. [*Note 1*: The creation of the transformed type is described in [[temp.func.order]](temp.func.order.md). — *end note*] The deduction process uses the transformed type as the argument template and the original type of the other template as the parameter template. This process is done twice for each type involved in the partial ordering comparison: once using the transformed template-1 as the argument template and template-2 as the parameter template and again using the transformed template-2 as the argument template and template-1 as the parameter template.

**3** The types used to determine the ordering depend on the context in which the partial ordering is done:

- In the context of a function call, the types used are those function parameter types for which the function call has arguments.[^1]
- In the context of a call to a conversion function, the return types of the conversion function templates are used.
- In other contexts ([[temp.func.order]](temp.func.order.md)) the function template’s function type is used.

**4** Each type nominated above from the parameter template and the corresponding type from the argument template are used as the types of `P` and `A`.

**5** Before the partial ordering is done, certain transformations are performed on the types used for partial ordering:

- If `P` is a reference type, `P` is replaced by the type referred to.
- If `A` is a reference type, `A` is replaced by the type referred to.

**6** If both `P` and `A` were reference types (before being replaced with the type referred to above), determine which of the two types (if any) is more cv-qualified than the other; otherwise the types are considered to be equally cv-qualified for partial ordering purposes. The result of this determination will be used below.

**7** Remove any top-level cv-qualifiers:

- If `P` is a cv-qualified type, `P` is replaced by the cv-unqualified version of `P`.
- If `A` is a cv-qualified type, `A` is replaced by the cv-unqualified version of `A`.

**8** Using the resulting types `P` and `A`, the deduction is then done as described in [[temp.deduct.type]](temp.deduct.type.md). If `P` is a function parameter pack, the type `A` of each remaining parameter type of the argument template is compared with the type `P` of the *declarator-id* of the function parameter pack. Each comparison deduces template arguments for subsequent positions in the template parameter packs expanded by the function parameter pack. Similarly, if `A` was transformed from a function parameter pack, it is compared with each remaining parameter type of the parameter template. If deduction succeeds for a given type, the type from the argument template is considered to be at least as specialized as the type from the parameter template. [*Example 1*:

```cpp
template<class... Args>           void f(Args... args);         // #1
template<class T1, class... Args> void f(T1 a1, Args... args);  // #2
template<class T1, class T2>      void f(T1 a1, T2 a2);         // #3

f();                // calls #1
f(1, 2, 3);         // calls #2
f(1, 2);            // calls #3; non-variadic template #3 is more specialized
                    // than the variadic templates #1 and #2
```

— *end example*]

**9** If, for a given type, the types are identical after the transformations above and both `P` and `A` were reference types (before being replaced with the type referred to above):

- if the type from the argument template was an lvalue reference and the type from the parameter template was not, the parameter type is not considered to be at least as specialized as the argument type; otherwise,
- if the type from the argument template is more cv-qualified than the type from the parameter template (as described above), the parameter type is not considered to be at least as specialized as the argument type.

**10** Function template `F` is *at least as specialized as* function template `G` if, for each pair of types used to determine the ordering, the type from `F` is at least as specialized as the type from `G`. `F` is *more specialized than* `G` if `F` is at least as specialized as `G` and `G` is not at least as specialized as `F`.

**11** If, after considering the above, function template `F` is at least as specialized as function template `G` and vice-versa, and if `G` has a trailing function parameter pack for which `F` does not have a corresponding parameter, and if `F` does not have a trailing function parameter pack, then `F` is more specialized than `G`.

**12** In most cases, deduction fails if not all template parameters have values, but for partial ordering purposes a template parameter may remain without a value provided it is not used in the types being used for partial ordering. [*Note 2*: A template parameter used in a non-deduced context is considered used. — *end note*] [*Example 2*:

```cpp
template <class T> T f(int);            // #1
template <class T, class U> T f(U);     // #2
void g() {
  f<int>(1);                            // calls #1
}
```

— *end example*]

**13** [*Note 3*: Partial ordering of function templates containing template parameter packs is independent of the number of deduced arguments for those template parameter packs. — *end note*] [*Example 3*:

```cpp
template<class ...> struct Tuple { };
template<class ... Types> void g(Tuple<Types ...>);                 // #1
template<class T1, class ... Types> void g(Tuple<T1, Types ...>);   // #2
template<class T1, class ... Types> void g(Tuple<T1, Types& ...>);  // #3

g(Tuple<>());                   // calls #1
g(Tuple<int, float>());         // calls #2
g(Tuple<int, float&>());        // calls #3
g(Tuple<int>());                // calls #3
```

— *end example*]

[^1]: Default arguments are not considered to be arguments in this context; they only become arguments after a function has been selected.
