# 11.4 Class members [class.mem]

[Contents](README.md) · up: [11 Classes \[class\]](class.md) · prev: [11.3 Class names \[class.name\]](class.name.md) · next: [11.4.1 Member functions \[class.mfct\]](class.mfct.md)

```bnf
member-specification:
    member-declaration member-specificationₒₚₜ
    access-specifier ':' member-specificationₒₚₜ
```

```bnf
member-declaration:
    attribute-specifier-seqₒₚₜ decl-specifier-seqₒₚₜ member-declarator-listₒₚₜ ';'
    function-definition
    using-declaration
    using-enum-declaration
    static_assert-declaration
    template-declaration
    explicit-specialization
    deduction-guide
    alias-declaration
    opaque-enum-declaration
    empty-declaration
```

```bnf
member-declarator-list:
    member-declarator
    member-declarator-list ',' member-declarator
```

```bnf
member-declarator:
    declarator virt-specifier-seqₒₚₜ pure-specifierₒₚₜ
    declarator requires-clause
    declarator brace-or-equal-initializerₒₚₜ
    identifierₒₚₜ attribute-specifier-seqₒₚₜ ':' constant-expression brace-or-equal-initializerₒₚₜ
```

```bnf
virt-specifier-seq:
    virt-specifier
    virt-specifier-seq virt-specifier
```

```bnf
virt-specifier:
    'override'
    'final'
```

```bnf
pure-specifier:
    '=' '0'
```

**1** The *member-specification* in a class definition declares the full set of members of the class; no member can be added elsewhere. A *direct member* of a class `X` is a member of `X` that was first declared within the *member-specification* of `X`, including anonymous union objects ([[class.union.anon]](class.union.anon.md)) and direct members thereof. Members of a class are data members, member functions ([[class.mfct]](class.mfct.md)), nested types, enumerators, and member templates ([[temp.mem]](temp.mem.md)) and specializations thereof. [*Note 1*: A specialization of a static data member template is a static data member. A specialization of a member function template is a member function. A specialization of a member class template is a nested class. — *end note*]

**2** A *member-declaration* does not declare new members of the class if it is

- a friend declaration ([[class.friend]](class.friend.md)),
- a *static\_assert-declaration*,
- a *using-declaration* ([[namespace.udecl]](namespace.udecl.md)), or
- an *empty-declaration*.

For any other *member-declaration*, each declared entity that is not an unnamed bit-field ([[class.bit]](class.bit.md)) is a member of the class, and each such *member-declaration* shall either declare at least one member name of the class or declare at least one unnamed bit-field.

**3** A *data member* is a non-function member introduced by a *member-declarator*. A *member function* is a member that is a function. Nested types are classes ([[class.name]](class.name.md), [[class.nest]](class.nest.md)) and enumerations ([[dcl.enum]](dcl.enum.md)) declared in the class and arbitrary types declared as members by use of a typedef declaration ([[dcl.typedef]](dcl.typedef.md)) or *alias-declaration*. The enumerators of an unscoped enumeration ([[dcl.enum]](dcl.enum.md)) defined in the class are members of the class.

**4** A data member or member function may be declared `static` in its *member-declaration*, in which case it is a *static member* (see [[class.static]](class.static.md)) (a *static data member* ([[class.static.data]](class.static.data.md)) or *static member function* ([[class.static.mfct]](class.static.mfct.md)), respectively) of the class. Any other data member or member function is a *non-static member* (a *non-static data member* or *non-static member function* ([[class.mfct.non-static]](class.mfct.non-static.md)), respectively). [*Note 2*: A non-static data member of non-reference type is a member subobject of a class object ([[intro.object]](intro.object.md)). — *end note*]

**5** A member shall not be declared twice in the *member-specification*, except that

- a nested class or member class template can be declared and then later defined, and
- an enumeration can be introduced with an *opaque-enum-declaration* and later redeclared with an *enum-specifier*.

[*Note 3*: A single name can denote several member functions provided their types are sufficiently different ([[over.load]](over.load.md)). — *end note*]

**6** A *complete-class context* of a class is a

- function body ([[dcl.fct.def.general]](dcl.fct.def.general.md)),
- default argument ([[dcl.fct.default]](dcl.fct.default.md)),
- *noexcept-specifier* ([[except.spec]](except.spec.md)), or
- default member initializer

within the *member-specification* of the class. [*Note 4*: A complete-class context of a nested class is also a complete-class context of any enclosing class, if the nested class is defined within the *member-specification* of the enclosing class. — *end note*]

**7** A class is considered a completely-defined object type ([[basic.types]](basic.types.md)) (or complete type) at the closing `}` of the *class-specifier*. The class is regarded as complete within its complete-class contexts; otherwise it is regarded as incomplete within its own class *member-specification*.

**8** In a *member-declarator*, an `=` immediately following the *declarator* is interpreted as introducing a *pure-specifier* if the *declarator-id* has function type, otherwise it is interpreted as introducing a *brace-or-equal-initializer*. [*Example 1*:

```cpp
struct S {
  using T = void();
  T * p = 0;        // OK: brace-or-equal-initializer
  virtual T f = 0;  // OK: pure-specifier
};
```

— *end example*]

**9** In a *member-declarator* for a bit-field, the *constant-expression* is parsed as the longest sequence of tokens that could syntactically form a *constant-expression*. [*Example 2*:

```cpp
int a;
const int b = 0;
struct S {
  int x1 : 8 = 42;              // OK, "= 42" is brace-or-equal-initializer
  int x2 : 8 { 42 };            // OK, "{ 42 }" is brace-or-equal-initializer
  int y1 : true ? 8 : a = 42;   // OK, brace-or-equal-initializer is absent
  int y2 : true ? 8 : b = 42;   // error: cannot assign to const int
  int y3 : (true ? 8 : b) = 42; // OK, "= 42" is brace-or-equal-initializer
  int z : 1 || new int { 0 };   // OK, brace-or-equal-initializer is absent
};
```

— *end example*]

**10** A *brace-or-equal-initializer* shall appear only in the declaration of a data member. (For static data members, see [[class.static.data]](class.static.data.md); for non-static data members, see [[class.base.init]](class.base.init.md) and [[dcl.init.aggr]](dcl.init.aggr.md)). A *brace-or-equal-initializer* for a non-static data member specifies a *default member initializer* for the member, and shall not directly or indirectly cause the implicit definition of a defaulted default constructor for the enclosing class or the exception specification of that constructor.

**11** A member shall not be declared with the `extern` *storage-class-specifier*. Within a class definition, a member shall not be declared with the `thread_local` *storage-class-specifier* unless also declared `static`.

**12** The *decl-specifier-seq* may be omitted in constructor, destructor, and conversion function declarations only; when declaring another kind of member the *decl-specifier-seq* shall contain a *type-specifier* that is not a *cv-qualifier*. The *member-declarator-list* can be omitted only after a *class-specifier* or an *enum-specifier* or in a friend declaration ([[class.friend]](class.friend.md)). A *pure-specifier* shall be used only in the declaration of a virtual function ([[class.virtual]](class.virtual.md)) that is not a friend declaration.

**13** The optional *attribute-specifier-seq* in a *member-declaration* appertains to each of the entities declared by the *member-declarator*s; it shall not appear if the optional *member-declarator-list* is omitted.

**14** A *virt-specifier-seq* shall contain at most one of each *virt-specifier*. A *virt-specifier-seq* shall appear only in the first declaration of a virtual member function ([[class.virtual]](class.virtual.md)).

**15** The type of a non-static data member shall not be an incomplete type ([[basic.types]](basic.types.md)), an abstract class type ([[class.abstract]](class.abstract.md)), or a (possibly multi-dimensional) array thereof. [*Note 5*: In particular, a class `C` cannot contain a non-static member of class `C`, but it can contain a pointer or reference to an object of class `C`. — *end note*]

**16** [*Note 6*: See [[expr.prim.id]](expr.prim.id.md) for restrictions on the use of non-static data members and non-static member functions. — *end note*]

**17** [*Note 7*: The type of a non-static member function is an ordinary function type, and the type of a non-static data member is an ordinary object type. There are no special member function types or data member types. — *end note*]

**18** [*Example 3*: A simple example of a class definition is

```cpp
struct tnode {
  char tword[20];
  int count;
  tnode* left;
  tnode* right;
};
```

which contains an array of twenty characters, an integer, and two pointers to objects of the same type. Once this definition has been given, the declaration

```cpp
tnode s, *sp;
```

declares `s` to be a `tnode` and `sp` to be a pointer to a `tnode`. With these declarations, `sp->count` refers to the `count` member of the object to which `sp` points; `s.left` refers to the `left` subtree pointer of the object `s`; and `s.right->tword[0]` refers to the initial character of the `tword` member of the `right` subtree of `s`. — *end example*]

**19** [*Note 8*: Non-static data members of a (non-union) class with the same access control ([[class.access]](class.access.md)) and non-zero size ([[intro.object]](intro.object.md)) are allocated so that later members have higher addresses within a class object. The order of allocation of non-static data members with different access control is unspecified. Implementation alignment requirements might cause two adjacent members not to be allocated immediately after each other; so might requirements for space for managing virtual functions ([[class.virtual]](class.virtual.md)) and virtual base classes ([[class.mi]](class.mi.md)). — *end note*]

**20** If `T` is the name of a class, then each of the following shall have a name different from `T`:

- every static data member of class `T`;
- every member function of class `T` [*Note 9*: This restriction does not apply to constructors, which do not have names ([[class.ctor]](class.ctor.md)) — *end note*];
- every member of class `T` that is itself a type;
- every member template of class `T`;
- every enumerator of every member of class `T` that is an unscoped enumerated type; and
- every member of every anonymous union that is a member of class `T`.

**21** In addition, if class `T` has a user-declared constructor ([[class.ctor]](class.ctor.md)), every non-static data member of class `T` shall have a name different from `T`.

**22** The *common initial sequence* of two standard-layout struct ([[class.prop]](class.prop.md)) types is the longest sequence of non-static data members and bit-fields in declaration order, starting with the first such entity in each of the structs, such that corresponding entities have layout-compatible types, either both entities are declared with the `no_unique_address` attribute ([[dcl.attr.nouniqueaddr]](dcl.attr.nouniqueaddr.md)) or neither is, and either both entities are bit-fields with the same width or neither is a bit-field. [*Example 4*:

```cpp
struct A { int a; char b; };
struct B { const int b1; volatile char b2; };
struct C { int c; unsigned : 0; char b; };
struct D { int d; char b : 4; };
struct E { unsigned int e; char b; };
```

The common initial sequence of `A` and `B` comprises all members of either class. The common initial sequence of `A` and `C` and of `A` and `D` comprises the first member in each case. The common initial sequence of `A` and `E` is empty. — *end example*]

**23** Two standard-layout struct ([[class.prop]](class.prop.md)) types are *layout-compatible classes* if their common initial sequence comprises all members and bit-fields of both classes ([[basic.types]](basic.types.md)).

**24** Two standard-layout unions are layout-compatible if they have the same number of non-static data members and corresponding non-static data members (in any order) have layout-compatible types ([[basic.types]](basic.types.md)).

**25** In a standard-layout union with an active member ([[class.union]](class.union.md)) of struct type `T1`, it is permitted to read a non-static data member `m` of another union member of struct type `T2` provided `m` is part of the common initial sequence of `T1` and `T2`; the behavior is as if the corresponding member of `T1` were nominated. [*Example 5*:

```cpp
struct T1 { int a, b; };
struct T2 { int c; double d; };
union U { T1 t1; T2 t2; };
int f() {
  U u = { { 1, 2 } };   // active member is t1
  return u.t2.c;        // OK, as if u.t1.a were nominated
}
```

— *end example*] [*Note 10*: Reading a volatile object through a glvalue of non-volatile type has undefined behavior ([[dcl.type.cv]](dcl.type.cv.md)). — *end note*]

**26** If a standard-layout class object has any non-static data members, its address is the same as the address of its first non-static data member if that member is not a bit-field. Its address is also the same as the address of each of its base class subobjects. [*Note 11*: There might therefore be unnamed padding within a standard-layout struct object inserted by an implementation, but not at its beginning, as necessary to achieve appropriate alignment. — *end note*] [*Note 12*: The object and its first subobject are pointer-interconvertible ([[basic.compound]](basic.compound.md), [[expr.static.cast]](expr.static.cast.md)). — *end note*]

## Subclauses

- [11.4.1 Member functions \[class.mfct\]](class.mfct.md)
- [11.4.2 Non-static member functions \[class.mfct.non-static\]](class.mfct.non-static.md)
  - [11.4.2.1 The `this` pointer \[class.this\]](class.this.md)
- [11.4.3 Special member functions \[special\]](special.md)
- [11.4.4 Constructors \[class.ctor\]](class.ctor.md)
  - [11.4.4.1 Default constructors \[class.default.ctor\]](class.default.ctor.md)
  - [11.4.4.2 Copy/move constructors \[class.copy.ctor\]](class.copy.ctor.md)
- [11.4.5 Copy/move assignment operator \[class.copy.assign\]](class.copy.assign.md)
- [11.4.6 Destructors \[class.dtor\]](class.dtor.md)
- [11.4.7 Conversions \[class.conv\]](class.conv.md)
  - [11.4.7.1 Conversion by constructor \[class.conv.ctor\]](class.conv.ctor.md)
  - [11.4.7.2 Conversion functions \[class.conv.fct\]](class.conv.fct.md)
- [11.4.8 Static members \[class.static\]](class.static.md)
  - [11.4.8.1 Static member functions \[class.static.mfct\]](class.static.mfct.md)
  - [11.4.8.2 Static data members \[class.static.data\]](class.static.data.md)
- [11.4.9 Bit-fields \[class.bit\]](class.bit.md)
- [11.4.10 Nested class declarations \[class.nest\]](class.nest.md)
- [11.4.11 Nested type names \[class.nested.type\]](class.nested.type.md)
