# 6.5.2 Argument-dependent name lookup [basic.lookup.argdep]

[Contents](README.md) · up: [6.5 Name lookup \[basic.lookup\]](basic.lookup.md) · prev: [6.5.1 Unqualified name lookup \[basic.lookup.unqual\]](basic.lookup.unqual.md) · next: [6.5.3 Qualified name lookup \[basic.lookup.qual\]](basic.lookup.qual.md)

**1** When the *postfix-expression* in a function call ([[expr.call]](expr.call.md)) is an *unqualified-id*, other namespaces not considered during the usual unqualified lookup ([[basic.lookup.unqual]](basic.lookup.unqual.md)) may be searched, and in those namespaces, namespace-scope friend function or function template declarations ([[class.friend]](class.friend.md)) not otherwise visible may be found. These modifications to the search depend on the types of the arguments (and for template template arguments, the namespace of the template argument). [*Example 1*:

```cpp
namespace N {
  struct S { };
  void f(S);
}

void g() {
  N::S s;
  f(s);             // OK: calls N::f
  (f)(s);           // error: N::f not considered; parentheses prevent argument-dependent lookup
}
```

— *end example*]

**2** For each argument type `T` in the function call, there is a set of zero or more *associated namespaces* and a set of zero or more *associated entities* (other than namespaces) to be considered. The sets of namespaces and entities are determined entirely by the types of the function arguments (and the namespace of any template template argument). Typedef names and *using-declaration*s used to specify the types do not contribute to this set. The sets of namespaces and entities are determined in the following way:

- If `T` is a fundamental type, its associated sets of namespaces and entities are both empty.
- If `T` is a class type (including unions), its associated entities are: the class itself; the class of which it is a member, if any; and its direct and indirect base classes. Its associated namespaces are the innermost enclosing namespaces of its associated entities. Furthermore, if `T` is a class template specialization, its associated namespaces and entities also include: the namespaces and entities associated with the types of the template arguments provided for template type parameters (excluding template template parameters); the templates used as template template arguments; the namespaces of which any template template arguments are members; and the classes of which any member templates used as template template arguments are members. [*Note 1*: Non-type template arguments do not contribute to the set of associated namespaces. — *end note*]
- If `T` is an enumeration type, its associated namespace is the innermost enclosing namespace of its declaration, and its associated entities are `T` and, if it is a class member, the member’s class.
- If `T` is a pointer to `U` or an array of `U`, its associated namespaces and entities are those associated with `U`.
- If `T` is a function type, its associated namespaces and entities are those associated with the function parameter types and those associated with the return type.
- If `T` is a pointer to a member function of a class `X`, its associated namespaces and entities are those associated with the function parameter types and return type, together with those associated with `X`.
- If `T` is a pointer to a data member of class `X`, its associated namespaces and entities are those associated with the member type together with those associated with `X`.

If an associated namespace is an inline namespace ([[namespace.def]](namespace.def.md)), its enclosing namespace is also included in the set. If an associated namespace directly contains inline namespaces, those inline namespaces are also included in the set. In addition, if the argument is the name or address of an overload set, its associated entities and namespaces are the union of those associated with each of the members of the set, i.e., the entities and namespaces associated with its parameter types and return type. Additionally, if the aforementioned overload set is named with a *template-id*, its associated entities and namespaces also include those of its type *template-argument*s and its template *template-argument*s.

**3** Let *X* be the lookup set produced by unqualified lookup ([[basic.lookup.unqual]](basic.lookup.unqual.md)) and let *Y* be the lookup set produced by argument dependent lookup (defined as follows). If *X* contains

- a declaration of a class member, or
- a block-scope function declaration that is not a *using-declaration*, or
- a declaration that is neither a function nor a function template

then *Y* is empty. Otherwise *Y* is the set of declarations found in the namespaces associated with the argument types as described below. The set of declarations found by the lookup of the name is the union of *X* and *Y*. [*Note 2*: The namespaces and entities associated with the argument types can include namespaces and entities already considered by the ordinary unqualified lookup. — *end note*] [*Example 2*:

```cpp
namespace NS {
  class T { };
  void f(T);
  void g(T, int);
}
NS::T parm;
void g(NS::T, float);
int main() {
  f(parm);                      // OK: calls NS::f
  extern void g(NS::T, float);
  g(parm, 1);                   // OK: calls g(NS::T, float)
}
```

— *end example*]

**4** When considering an associated namespace `N`, the lookup is the same as the lookup performed when `N` is used as a qualifier ([[namespace.qual]](namespace.qual.md)) except that:

- Any *using-directive*s in `N` are ignored.
- All names except those of (possibly overloaded) functions and function templates are ignored.
- Any namespace-scope friend functions or friend function templates ([[class.friend]](class.friend.md)) declared in classes with reachable definitions in the set of associated entities are visible within their respective namespaces even if they are not visible during an ordinary lookup ([[namespace.memdef]](namespace.memdef.md)).
- Any exported declaration `D` in `N` declared within the purview of a named module `M` ([[module.interface]](module.interface.md)) is visible if there is an associated entity attached to `M` with the same innermost enclosing non-inline namespace as `D`.
- If the lookup is for a dependent name ([[temp.dep]](temp.dep.md), [[temp.dep.candidate]](temp.dep.candidate.md)), any declaration `D` in `N` is visible if `D` would be visible to qualified name lookup ([[namespace.qual]](namespace.qual.md)) at any point in the instantiation context ([[module.context]](module.context.md)) of the lookup, unless `D` is declared in another translation unit, attached to the global module, and is either discarded ([[module.global.frag]](module.global.frag.md)) or has internal linkage.

**5** [*Example 3*:

*Translation unit #1:*

```cpp
export module M;
namespace R {
  export struct X {};
  export void f(X);
}
namespace S {
  export void f(R::X, R::X);
}
```

*Translation unit #2:*

```cpp
export module N;
import M;
export R::X make();
namespace R { static int g(X); }
export template<typename T, typename U> void apply(T t, U u) {
  f(t, u);
  g(t);
}
```

*Translation unit #3:*

```cpp
module Q;
import N;
namespace S {
  struct Z { template<typename T> operator T(); };
}
void test() {
  auto x = make();              // OK, decltype(x) is R::X in module M
  R::f(x);                      // error: R and R::f are not visible here
  f(x);                         // OK, calls R::f from interface of M
  f(x, S::Z());                 // error: S::f in module M not considered
                                // even though S is an associated namespace
  apply(x, S::Z());             // error: S::f is visible in instantiation context, but
                                // R::g has internal linkage and cannot be used outside TU #2
}
```

— *end example*]
