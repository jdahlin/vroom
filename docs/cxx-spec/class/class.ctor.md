# 11.4.4 Constructors [class.ctor]

[Contents](../README.md) · up: [11.4 Class members \[class.mem\]](class.mem.md) · prev: [11.4.3 Special member functions \[special\]](special.md) · next: [11.4.4.1 Default constructors \[class.default.ctor\]](class.default.ctor.md)

**1** A *constructor* is introduced by a declaration whose *declarator* is a function declarator ([[dcl.fct]](../dcl/dcl.fct.md)) of the form

```bnf
ptr-declarator '(' parameter-declaration-clause ')' noexcept-specifierₒₚₜ attribute-specifier-seqₒₚₜ
```

where the *ptr-declarator* consists solely of an *id-expression*, an optional *attribute-specifier-seq*, and optional surrounding parentheses, and the *id-expression* has one of the following forms:

- in a *member-declaration* that belongs to the *member-specification* of a class or class template but is not a friend declaration ([[class.friend]](class.friend.md)), the *id-expression* is the injected-class-name ([[class.pre]](class.pre.md)) of the immediately-enclosing entity or
- in a declaration at namespace scope or in a friend declaration, the *id-expression* is a *qualified-id* that names a constructor ([[class.qual]](../basic/class.qual.md)).

Constructors do not have names. In a constructor declaration, each *decl-specifier* in the optional *decl-specifier-seq* shall be `friend`, `inline`, `constexpr`, or an *explicit-specifier*. [*Example 1*:

```cpp
struct S {
  S();              // declares the constructor
};

S::S() { }          // defines the constructor
```

— *end example*]

**2** A constructor is used to initialize objects of its class type. Because constructors do not have names, they are never found during name lookup; however an explicit type conversion using the functional notation ([[expr.type.conv]](../expr/expr.type.conv.md)) will cause a constructor to be called to initialize an object. [*Note 1*: The syntax looks like an explicit call of the constructor. — *end note*] [*Example 2*:

```cpp
complex zz = complex(1,2.3);
cprint( complex(7.8,1.2) );
```

— *end example*] [*Note 2*: For initialization of objects of class type see [[class.init]](class.init.md). — *end note*]

**3** An object created in this way is unnamed. [*Note 3*: [[class.temporary]](../basic/class.temporary.md) describes the lifetime of temporary objects. — *end note*] [*Note 4*: Explicit constructor calls do not yield lvalues, see [[basic.lval]](../expr/basic.lval.md). — *end note*]

**4** [*Note 5*: Some language constructs have special semantics when used during construction; see [[class.base.init]](class.base.init.md) and [[class.cdtor]](class.cdtor.md). — *end note*]

**5** A constructor can be invoked for a `const`, `volatile` or `const` `volatile` object. `const` and `volatile` semantics ([[dcl.type.cv]](../dcl/dcl.type.cv.md)) are not applied on an object under construction. They come into effect when the constructor for the most derived object ([[intro.object]](../basic/intro.object.md)) ends.

**6** A `return` statement in the body of a constructor shall not specify a return value. The address of a constructor shall not be taken.

**7** A constructor shall not be a coroutine.

## Subclauses

- [11.4.4.1 Default constructors \[class.default.ctor\]](class.default.ctor.md)
- [11.4.4.2 Copy/move constructors \[class.copy.ctor\]](class.copy.ctor.md)
