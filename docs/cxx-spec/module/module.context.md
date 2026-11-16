# 10.6 Instantiation context [module.context]

[Contents](../README.md) · up: [10 Modules \[module\]](module.md) · prev: [10.5 Private module fragment \[module.private.frag\]](module.private.frag.md) · next: [10.7 Reachability \[module.reach\]](module.reach.md)

**1** The *instantiation context* is a set of points within the program that determines which names are visible to argument-dependent name lookup ([[basic.lookup.argdep]](../basic/basic.lookup.argdep.md)) and which declarations are reachable ([[module.reach]](module.reach.md)) in the context of a particular declaration or template instantiation.

**2** During the implicit definition of a defaulted function ([[special]](../class/special.md), [[class.compare.default]](../class/class.compare.default.md)), the instantiation context is the union of the instantiation context from the definition of the class and the instantiation context of the program construct that resulted in the implicit definition of the defaulted function.

**3** During the implicit instantiation of a template whose point of instantiation is specified as that of an enclosing specialization ([[temp.point]](../temp/temp.point.md)), the instantiation context is the union of the instantiation context of the enclosing specialization and, if the template is defined in a module interface unit of a module *M* and the point of instantiation is not in a module interface unit of *M*, the point at the end of the *declaration-seq* of the primary module interface unit of *M* (prior to the *private-module-fragment*, if any).

**4** During the implicit instantiation of a template that is implicitly instantiated because it is referenced from within the implicit definition of a defaulted function, the instantiation context is the instantiation context of the defaulted function.

**5** During the instantiation of any other template specialization, the instantiation context comprises the point of instantiation of the template.

**6** In any other case, the instantiation context at a point within the program comprises that point.

**7** [*Example 1*:

*Translation unit #1:*

```cpp
export module stuff;
export template<typename T, typename U> void foo(T, U u) { auto v = u; }
export template<typename T, typename U> void bar(T, U u) { auto v = *u; }
```

*Translation unit #2:*

```cpp
export module M1;
import "defn.h";        // provides struct X {};
import stuff;
export template<typename T> void f(T t) {
  X x;
  foo(t, x);
}
```

*Translation unit #3:*

```cpp
export module M2;
import "decl.h";        // provides struct X; (not a definition)
import stuff;
export template<typename T> void g(T t) {
  X *x;
  bar(t, x);
}
```

*Translation unit #4:*

```cpp
import M1;
import M2;
void test() {
  f(0);
  g(0);
}
```

The call to `f(0)` is valid; the instantiation context of `foo<int, X>` comprises

- the point at the end of translation unit #1,
- the point at the end of translation unit #2, and
- the point of the call to `f(0)`,

so the definition of `X` is reachable ([[module.reach]](module.reach.md)).

It is unspecified whether the call to `g(0)` is valid: the instantiation context of `bar<int, X>` comprises

- the point at the end of translation unit #1,
- the point at the end of translation unit #3, and
- the point of the call to `g(0)`,

so the definition of `X` need not be reachable, as described in [[module.reach]](module.reach.md). — *end example*]
