# 13.9.3 Explicit specialization [temp.expl.spec]

[Contents](../README.md) · up: [13.9 Template instantiation and specialization \[temp.spec\]](temp.spec.md) · prev: [13.9.2 Explicit instantiation \[temp.explicit\]](temp.explicit.md) · next: [13.10 Function template specializations \[temp.fct.spec\]](temp.fct.spec.md)

**1** An explicit specialization of any of the following:

- function template
- class template
- variable template
- member function of a class template
- static data member of a class template
- member class of a class template
- member enumeration of a class template
- member class template of a class or class template
- member function template of a class or class template

can be declared by a declaration introduced by `template<>`; that is:

```bnf
explicit-specialization:
  'template' '<' '>' declaration
```

[*Example 1*:

```cpp
template<class T> class stream;

template<> class stream<char> { /* ... */ };

template<class T> class Array { /* ... */ };
template<class T> void sort(Array<T>& v) { /* ... */ }

template<> void sort<char*>(Array<char*>&);
```

Given these declarations, `stream<char>` will be used as the definition of streams of `char`s; other streams will be handled by class template specializations instantiated from the class template. Similarly, `sort<char*>` will be used as the sort function for arguments of type `Array<char*>`; other `Array` types will be sorted by functions generated from the template. — *end example*]

**2** An explicit specialization shall not use a *storage-class-specifier* ([[dcl.stc]](../dcl/dcl.stc.md)) other than `thread_local`.

**3** An explicit specialization may be declared in any scope in which the corresponding primary template may be defined ([[namespace.memdef]](../dcl/namespace.memdef.md), [[class.mem]](../class/class.mem.md), [[temp.mem]](temp.mem.md)).

**4** A declaration of a function template, class template, or variable template being explicitly specialized shall precede the declaration of the explicit specialization. [*Note 1*: A declaration, but not a definition of the template is required. — *end note*] The definition of a class or class template shall precede the declaration of an explicit specialization for a member template of the class or class template. [*Example 2*:

```cpp
template<> class X<int> { /* ... */ };          // error: X not a template

template<class T> class X;

template<> class X<char*> { /* ... */ };        // OK: X is a template
```

— *end example*]

**5** A member function, a member function template, a member class, a member enumeration, a member class template, a static data member, or a static data member template of a class template may be explicitly specialized for a class specialization that is implicitly instantiated; in this case, the definition of the class template shall precede the explicit specialization for the member of the class template. If such an explicit specialization for the member of a class template names an implicitly-declared special member function ([[special]](../class/special.md)), the program is ill-formed.

**6** A member of an explicitly specialized class is not implicitly instantiated from the member declaration of the class template; instead, the member of the class template specialization shall itself be explicitly defined if its definition is required. In this case, the definition of the class template explicit specialization shall be in scope at the point at which the member is defined. The definition of an explicitly specialized class is unrelated to the definition of a generated specialization. That is, its members need not have the same names, types, etc. as the members of a generated specialization. Members of an explicitly specialized class template are defined in the same manner as members of normal classes, and not using the `template<>` syntax. The same is true when defining a member of an explicitly specialized member class. However, `template<>` is used in defining a member of an explicitly specialized member class template that is specialized as a class template. [*Example 3*:

```cpp
template<class T> struct A {
  struct B { };
  template<class U> struct C { };
};

template<> struct A<int> {
  void f(int);
};

void h() {
  A<int> a;
  a.f(16);          // A<int>::f must be defined somewhere
}

// template<> not used for a member of an explicitly specialized class template
void A<int>::f(int) { /* ... */ }

template<> struct A<char>::B {
  void f();
};
// template<> also not used when defining a member of an explicitly specialized member class
void A<char>::B::f() { /* ... */ }

template<> template<class U> struct A<char>::C {
  void f();
};
// template<> is used when defining a member of an explicitly specialized member class template
// specialized as a class template
template<>
template<class U> void A<char>::C<U>::f() { /* ... */ }

template<> struct A<short>::B {
  void f();
};
template<> void A<short>::B::f() { /* ... */ }              // error: template<> not permitted

template<> template<class U> struct A<short>::C {
  void f();
};
template<class U> void A<short>::C<U>::f() { /* ... */ }    // error: template<> required
```

— *end example*]

**7** If a template, a member template or a member of a class template is explicitly specialized then that specialization shall be declared before the first use of that specialization that would cause an implicit instantiation to take place, in every translation unit in which such a use occurs; no diagnostic is required. If the program does not provide a definition for an explicit specialization and either the specialization is used in a way that would cause an implicit instantiation to take place or the member is a virtual member function, the program is ill-formed, no diagnostic required. An implicit instantiation is never generated for an explicit specialization that is declared but not defined. [*Example 4*:

```cpp
class String { };
template<class T> class Array { /* ... */ };
template<class T> void sort(Array<T>& v) { /* ... */ }

void f(Array<String>& v) {
  sort(v);          // use primary template sort(Array<T>&), T is String
}

template<> void sort<String>(Array<String>& v);     // error: specialization after use of primary template
template<> void sort<>(Array<char*>& v);            // OK: sort<char*> not yet used
template<class T> struct A {
  enum E : T;
  enum class S : T;
};
template<> enum A<int>::E : int { eint };           // OK
template<> enum class A<int>::S : int { sint };     // OK
template<class T> enum A<T>::E : T { eT };
template<class T> enum class A<T>::S : T { sT };
template<> enum A<char>::E : char { echar };        // error: A<char>::E was instantiated
                                                    // when A<char> was instantiated
template<> enum class A<char>::S : char { schar };  // OK
```

— *end example*]

**8** The placement of explicit specialization declarations for function templates, class templates, variable templates, member functions of class templates, static data members of class templates, member classes of class templates, member enumerations of class templates, member class templates of class templates, member function templates of class templates, static data member templates of class templates, member functions of member templates of class templates, member functions of member templates of non-template classes, static data member templates of non-template classes, member function templates of member classes of class templates, etc., and the placement of partial specialization declarations of class templates, variable templates, member class templates of non-template classes, static data member templates of non-template classes, member class templates of class templates, etc., can affect whether a program is well-formed according to the relative positioning of the explicit specialization declarations and their points of instantiation in the translation unit as specified above and below. When writing a specialization, be careful about its location; or to make it compile will be such a trial as to kindle its self-immolation.

**9** A template explicit specialization is in the scope of the namespace in which the template was defined. [*Example 5*:

```cpp
namespace N {
  template<class T> class X { /* ... */ };
  template<class T> class Y { /* ... */ };

  template<> class X<int> { /* ... */ };        // OK: specialization in same namespace
  template<> class Y<double>;                   // forward-declare intent to specialize for double
}

template<> class N::Y<double> { /* ... */ };    // OK: specialization in enclosing namespace
template<> class N::Y<short> { /* ... */ };     // OK: specialization in enclosing namespace
```

— *end example*]

**10** A *simple-template-id* that names a class template explicit specialization that has been declared but not defined can be used exactly like the names of other incompletely-defined classes ([[basic.types]](../basic/basic.types.md)). [*Example 6*:

```cpp
template<class T> class X;                      // X is a class template
template<> class X<int>;

X<int>* p;                                      // OK: pointer to declared class X<int>
X<int> x;                                       // error: object of incomplete class X<int>
```

— *end example*]

**11** A trailing *template-argument* can be left unspecified in the *template-id* naming an explicit function template specialization provided it can be deduced from the function argument type. [*Example 7*:

```cpp
template<class T> class Array { /* ... */ };
template<class T> void sort(Array<T>& v);

// explicit specialization for sort(Array<int>&)
// with deduced template-argument of type int
template<> void sort(Array<int>&);
```

— *end example*]

**12** [*Note 2*: An explicit specialization of a constrained template is required to satisfy that template’s associated constraints ([[temp.constr.decl]](temp.constr.decl.md)). The satisfaction of constraints is determined when forming the template name of an explicit specialization in which all template arguments are specified ([[temp.names]](temp.names.md)), or, for explicit specializations of function templates, during template argument deduction ([[temp.deduct.decl]](temp.deduct.decl.md)) when one or more trailing template arguments are left unspecified. — *end note*]

**13** A function with the same name as a template and a type that exactly matches that of a template specialization is not an explicit specialization ([[temp.fct]](temp.fct.md)).

**14** Whether an explicit specialization of a function or variable template is inline, constexpr, or an immediate function is determined by the explicit specialization and is independent of those properties of the template. [*Example 8*:

```cpp
template<class T> void f(T) { /* ... */ }
template<class T> inline T g(T) { /* ... */ }

template<> inline void f<>(int) { /* ... */ }   // OK: inline
template<> int g<>(int) { /* ... */ }           // OK: not inline
```

— *end example*]

**15** An explicit specialization of a static data member of a template or an explicit specialization of a static data member template is a definition if the declaration includes an initializer; otherwise, it is a declaration. [*Note 3*: The definition of a static data member of a template that requires default-initialization must use a *braced-init-list*:

```cpp
template<> X Q<int>::x;                         // declaration
template<> X Q<int>::x ();                      // error: declares a function
template<> X Q<int>::x { };                     // definition
```

— *end note*]

**16** A member or a member template of a class template may be explicitly specialized for a given implicit instantiation of the class template, even if the member or member template is defined in the class template definition. An explicit specialization of a member or member template is specified using the syntax for explicit specialization. [*Example 9*:

```cpp
template<class T> struct A {
  void f(T);
  template<class X1> void g1(T, X1);
  template<class X2> void g2(T, X2);
  void h(T) { }
};

// specialization
template<> void A<int>::f(int);

// out of class member template definition
template<class T> template<class X1> void A<T>::g1(T, X1) { }

// member template specialization
template<> template<class X1> void A<int>::g1(int, X1);

// member template specialization
template<> template<>
  void A<int>::g1(int, char);           // X1 deduced as char
template<> template<>
  void A<int>::g2<char>(int, char);     // X2 specified as char

// member specialization even if defined in class definition
template<> void A<int>::h(int) { }
```

— *end example*]

**17** A member or a member template may be nested within many enclosing class templates. In an explicit specialization for such a member, the member declaration shall be preceded by a `template<>` for each enclosing class template that is explicitly specialized. [*Example 10*:

```cpp
template<class T1> class A {
  template<class T2> class B {
    void mf();
  };
};
template<> template<> class A<int>::B<double>;
template<> template<> void A<char>::B<char>::mf();
```

— *end example*]

**18** In an explicit specialization declaration for a member of a class template or a member template that appears in namespace scope, the member template and some of its enclosing class templates may remain unspecialized, except that the declaration shall not explicitly specialize a class member template if its enclosing class templates are not explicitly specialized as well. In such an explicit specialization declaration, the keyword `template` followed by a *template-parameter-list* shall be provided instead of the `template<>` preceding the explicit specialization declaration of the member. The types of the *template-parameter*s in the *template-parameter-list* shall be the same as those specified in the primary template definition. [*Example 11*:

```cpp
template <class T1> class A {
  template<class T2> class B {
    template<class T3> void mf1(T3);
    void mf2();
  };
};
template <> template <class X>
  class A<int>::B {
      template <class T> void mf1(T);
  };
template <> template <> template<class T>
  void A<int>::B<double>::mf1(T t) { }
template <class Y> template <>
  void A<Y>::B<double>::mf2() { }       // error: B<double> is specialized but
                                        // its enclosing class template A is not
```

— *end example*]

**19** A specialization of a member function template, member class template, or static data member template of a non-specialized class template is itself a template.

**20** An explicit specialization declaration shall not be a friend declaration.

**21** Default function arguments shall not be specified in a declaration or a definition for one of the following explicit specializations:

- the explicit specialization of a function template;
- the explicit specialization of a member function template;
- the explicit specialization of a member function of a class template where the class template specialization to which the member function specialization belongs is implicitly instantiated. [*Note 4*: Default function arguments may be specified in the declaration or definition of a member function of a class template specialization that is explicitly specialized. — *end note*]
