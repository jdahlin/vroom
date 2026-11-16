# 9.2.8.6 Deduced class template specialization types [dcl.type.class.deduct]

[Contents](README.md) · up: [9.2.8 Type specifiers \[dcl.type\]](dcl.type.md) · prev: [9.2.8.5.1 Placeholder type deduction \[dcl.type.auto.deduct\]](dcl.type.auto.deduct.md) · next: [9.3 Declarators \[dcl.decl\]](dcl.decl.md)

**1** If a placeholder for a deduced class type appears as a *decl-specifier* in the *decl-specifier-seq* of an initializing declaration ([[dcl.init]](dcl.init.md)) of a variable, the declared type of the variable shall be *cv* `T`, where `T` is the placeholder. [*Example 1*:

```cpp
template <class ...T> struct A {
  A(T...) {}
};
A x[29]{};          // error: no declarator operators allowed
const A& y{};       // error: no declarator operators allowed
```

— *end example*] The placeholder is replaced by the return type of the function selected by overload resolution for class template deduction ([[over.match.class.deduct]](over.match.class.deduct.md)). If the *decl-specifier-seq* is followed by an *init-declarator-list* or *member-declarator-list* containing more than one *declarator*, the type that replaces the placeholder shall be the same in each deduction.

**2** A placeholder for a deduced class type can also be used in the *type-specifier-seq* in the *new-type-id* or *type-id* of a *new-expression* ([[expr.new]](expr.new.md)), as the *simple-type-specifier* in an explicit type conversion (functional notation) ([[expr.type.conv]](expr.type.conv.md)), or as the *type-specifier* in the *parameter-declaration* of a *template-parameter* ([[temp.param]](temp.param.md)). A placeholder for a deduced class type shall not appear in any other context.

**3** [*Example 2*:

```cpp
template<class T> struct container {
    container(T t) {}
    template<class Iter> container(Iter beg, Iter end);
};
template<class Iter>
container(Iter b, Iter e) -> container<typename std::iterator_traits<Iter>::value_type>;
std::vector<double> v = { /* ... */ };

container c(7);                         // OK, deduces int for T
auto d = container(v.begin(), v.end()); // OK, deduces double for T
container e{5, 6};                      // error: int is not an iterator
```

— *end example*]
