# 13.9 Template instantiation and specialization [temp.spec]

[Contents](README.md) · up: [13 Templates \[temp\]](temp.md) · prev: [13.8.5 Friend names declared within a class template \[temp.inject\]](temp.inject.md) · next: [13.9.1 Implicit instantiation \[temp.inst\]](temp.inst.md)

**1** The act of instantiating a function, a variable, a class, a member of a class template, or a member template is referred to as *template instantiation*.

**2** A function instantiated from a function template is called an instantiated function. A class instantiated from a class template is called an instantiated class. A member function, a member class, a member enumeration, or a static data member of a class template instantiated from the member definition of the class template is called, respectively, an instantiated member function, member class, member enumeration, or static data member. A member function instantiated from a member function template is called an instantiated member function. A member class instantiated from a member class template is called an instantiated member class. A variable instantiated from a variable template is called an instantiated variable. A static data member instantiated from a static data member template is called an instantiated static data member.

**3** An explicit specialization may be declared for a function template, a variable template, a class template, a member of a class template, or a member template. An explicit specialization declaration is introduced by `template<>`. In an explicit specialization declaration for a variable template, a class template, a member of a class template or a class member template, the name of the variable or class that is explicitly specialized shall be a *simple-template-id*. In the explicit specialization declaration for a function template or a member function template, the name of the function or member function explicitly specialized may be a *template-id*. [*Example 1*:

```cpp
template<class T = int> struct A {
  static int x;
};
template<class U> void g(U) { }

template<> struct A<double> { };        // specialize for T == double
template<> struct A<> { };              // specialize for T == int
template<> void g(char) { }             // specialize for U == char
                                        // U is deduced from the parameter type
template<> void g<int>(int) { }         // specialize for U == int
template<> int A<char>::x = 0;          // specialize for T == char

template<class T = int> struct B {
  static int x;
};
template<> int B<>::x = 1;              // specialize for T == int
```

— *end example*]

**4** An instantiated template specialization can be either implicitly instantiated ([[temp.inst]](temp.inst.md)) for a given argument list or be explicitly instantiated ([[temp.explicit]](temp.explicit.md)). A *specialization* is a class, variable, function, or class member that is either instantiated ([[temp.inst]](temp.inst.md)) from a templated entity or is an explicit specialization ([[temp.expl.spec]](temp.expl.spec.md)) of a templated entity.

**5** For a given template and a given set of *template-argument*s,

- an explicit instantiation definition shall appear at most once in a program,
- an explicit specialization shall be defined at most once in a program, as specified in [[basic.def.odr]](basic.def.odr.md), and
- both an explicit instantiation and a declaration of an explicit specialization shall not appear in a program unless the explicit instantiation follows a declaration of the explicit specialization.

An implementation is not required to diagnose a violation of this rule.

**6** The usual access checking rules do not apply to names in a declaration of an explicit instantiation or explicit specialization, with the exception of names appearing in a function body, default argument, base-clause, member-specification, enumerator-list, or static data member or variable template initializer. [*Note 1*: In particular, the template arguments and names used in the function declarator (including parameter types, return types and exception specifications) may be private types or objects that would normally not be accessible. — *end note*]

**7** Each class template specialization instantiated from a template has its own copy of any static members. [*Example 2*:

```cpp
template<class T> class X {
  static T s;
};
template<class T> T X<T>::s = 0;
X<int> aa;
X<char*> bb;
```

`X<int>` has a static member `s` of type `int` and `X<char*>` has a static member `s` of type `char*`. — *end example*]

**8** If a function declaration acquired its function type through a dependent type ([[temp.dep.type]](temp.dep.type.md)) without using the syntactic form of a function declarator, the program is ill-formed. [*Example 3*:

```cpp
template<class T> struct A {
  static T t;
};
typedef int function();
A<function> a;      // error: would declare A<function>::t as a static member function
```

— *end example*]

## Subclauses

- [13.9.1 Implicit instantiation \[temp.inst\]](temp.inst.md)
- [13.9.2 Explicit instantiation \[temp.explicit\]](temp.explicit.md)
- [13.9.3 Explicit specialization \[temp.expl.spec\]](temp.expl.spec.md)
