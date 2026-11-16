# 11.4.6 Destructors [class.dtor]

[Contents](../README.md) · up: [11.4 Class members \[class.mem\]](class.mem.md) · prev: [11.4.5 Copy/move assignment operator \[class.copy.assign\]](class.copy.assign.md) · next: [11.4.7 Conversions \[class.conv\]](class.conv.md)

**1** A *prospective destructor* is introduced by a declaration whose *declarator* is a function declarator ([[dcl.fct]](../dcl/dcl.fct.md)) of the form

```bnf
ptr-declarator '(' parameter-declaration-clause ')' noexcept-specifierₒₚₜ attribute-specifier-seqₒₚₜ
```

where the *ptr-declarator* consists solely of an *id-expression*, an optional *attribute-specifier-seq*, and optional surrounding parentheses, and the *id-expression* has one of the following forms:

- in a *member-declaration* that belongs to the *member-specification* of a class or class template but is not a friend declaration ([[class.friend]](class.friend.md)), the *id-expression* is `~`*class-name* and the *class-name* is the injected-class-name ([[class.pre]](class.pre.md)) of the immediately-enclosing entity or
- in a declaration at namespace scope or in a friend declaration, the *id-expression* is *nested-name-specifier* `~`*class-name* and the *class-name* names the same class as the *nested-name-specifier*.

A prospective destructor shall take no arguments ([[dcl.fct]](../dcl/dcl.fct.md)). Each *decl-specifier* of the *decl-specifier-seq* of a prospective destructor declaration (if any) shall be `friend`, `inline`, `virtual`, `constexpr`, or `consteval`.

**2** If a class has no user-declared prospective destructor, a prospective destructor is implicitly declared as defaulted ([[dcl.fct.def]](../dcl/dcl.fct.def.md)). An implicitly-declared prospective destructor is an inline public member of its class.

**3** An implicitly-declared prospective destructor for a class `X` will have the form

```cpp
~X()
```

**4** At the end of the definition of a class, overload resolution is performed among the prospective destructors declared in that class with an empty argument list to select the *destructor* for the class, also known as the *selected destructor*. The program is ill-formed if overload resolution fails. Destructor selection does not constitute a reference to, or odr-use ([[basic.def.odr]](../basic/basic.def.odr.md)) of, the selected destructor, and in particular, the selected destructor may be deleted ([[dcl.fct.def.delete]](../dcl/dcl.fct.def.delete.md)).

**5** The address of a destructor shall not be taken. A destructor can be invoked for a `const`, `volatile` or `const` `volatile` object. `const` and `volatile` semantics ([[dcl.type.cv]](../dcl/dcl.type.cv.md)) are not applied on an object under destruction. They stop being in effect when the destructor for the most derived object ([[intro.object]](../basic/intro.object.md)) starts.

**6** [*Note 1*: A declaration of a destructor that does not have a *noexcept-specifier* has the same exception specification as if it had been implicitly declared ([[except.spec]](../except/except.spec.md)). — *end note*]

**7** A defaulted destructor for a class `X` is defined as deleted if:

- `X` is a union-like class that has a variant member with a non-trivial destructor,
- any potentially constructed subobject has class type `M` (or array thereof) and `M` has a deleted destructor or a destructor that is inaccessible from the defaulted destructor,
- or, for a virtual destructor, lookup of the non-array deallocation function results in an ambiguity or in a function that is deleted or inaccessible from the defaulted destructor.

**8** A destructor is trivial if it is not user-provided and if:

- the destructor is not `virtual`,
- all of the direct base classes of its class have trivial destructors, and
- for all of the non-static data members of its class that are of class type (or array thereof), each such class has a trivial destructor.

Otherwise, the destructor is *non-trivial*.

**9** A defaulted destructor is a constexpr destructor if it satisfies the requirements for a constexpr destructor ([[dcl.constexpr]](../dcl/dcl.constexpr.md)).

**10** A destructor that is defaulted and not defined as deleted is *implicitly defined* when it is odr-used ([[basic.def.odr]](../basic/basic.def.odr.md)) or when it is explicitly defaulted after its first declaration.

**11** Before a defaulted destructor for a class is implicitly defined, all the non-user-provided destructors for its base classes and its non-static data members are implicitly defined.

**12** A prospective destructor can be declared `virtual` ([[class.virtual]](class.virtual.md)) or pure `virtual` ([[class.abstract]](class.abstract.md)). If the destructor of a class is virtual and any objects of that class or any derived class are created in the program, the destructor shall be defined. If a class has a base class with a virtual destructor, its destructor (whether user- or implicitly-declared) is virtual.

**13** [*Note 2*: Some language constructs have special semantics when used during destruction; see [[class.cdtor]](class.cdtor.md). — *end note*]

**14** After executing the body of the destructor and destroying any objects with automatic storage duration allocated within the body, a destructor for class `X` calls the destructors for `X`’s direct non-variant non-static data members, the destructors for `X`’s non-virtual direct base classes and, if `X` is the most derived class ([[class.base.init]](class.base.init.md)), its destructor calls the destructors for `X`’s virtual base classes. All destructors are called as if they were referenced with a qualified name, that is, ignoring any possible virtual overriding destructors in more derived classes. Bases and members are destroyed in the reverse order of the completion of their constructor (see [[class.base.init]](class.base.init.md)). A `return` statement ([[stmt.return]](../stmt/stmt.return.md)) in a destructor might not directly return to the caller; before transferring control to the caller, the destructors for the members and bases are called. Destructors for elements of an array are called in reverse order of their construction (see [[class.init]](class.init.md)).

**15** A destructor is invoked implicitly

- for a constructed object with static storage duration ([[basic.stc.static]](../basic/basic.stc.static.md)) at program termination ([[basic.start.term]](../basic/basic.start.term.md)),
- for a constructed object with thread storage duration ([[basic.stc.thread]](../basic/basic.stc.thread.md)) at thread exit,
- for a constructed object with automatic storage duration ([[basic.stc.auto]](../basic/basic.stc.auto.md)) when the block in which an object is created exits ([[stmt.dcl]](../stmt/stmt.dcl.md)),
- for a constructed temporary object when its lifetime ends ([[conv.rval]](../expr/conv.rval.md), [[class.temporary]](../basic/class.temporary.md)).

In each case, the context of the invocation is the context of the construction of the object. A destructor may also be invoked implicitly through use of a *delete-expression* ([[expr.delete]](../expr/expr.delete.md)) for a constructed object allocated by a *new-expression* ([[expr.new]](../expr/expr.new.md)); the context of the invocation is the *delete-expression*. [*Note 3*: An array of class type contains several subobjects for each of which the destructor is invoked. — *end note*] A destructor can also be invoked explicitly. A destructor is *potentially invoked* if it is invoked or as specified in [[expr.new]](../expr/expr.new.md), [[stmt.return]](../stmt/stmt.return.md), [[dcl.init.aggr]](../dcl/dcl.init.aggr.md), [[class.base.init]](class.base.init.md), and [[except.throw]](../except/except.throw.md). A program is ill-formed if a destructor that is potentially invoked is deleted or not accessible from the context of the invocation.

**16** At the point of definition of a virtual destructor (including an implicit definition ([[class.dtor]](class.dtor.md))), the non-array deallocation function is determined as if for the expression `delete this` appearing in a non-virtual destructor of the destructor’s class (see [[expr.delete]](../expr/expr.delete.md)). If the lookup fails or if the deallocation function has a deleted definition ([[dcl.fct.def]](../dcl/dcl.fct.def.md)), the program is ill-formed. [*Note 4*: This assures that a deallocation function corresponding to the dynamic type of an object is available for the *delete-expression* ([[class.free]](class.free.md)). — *end note*]

**17** In an explicit destructor call, the destructor is specified by a `~` followed by a *type-name* or *decltype-specifier* that denotes the destructor’s class type. The invocation of a destructor is subject to the usual rules for member functions ([[class.mfct]](class.mfct.md)); that is, if the object is not of the destructor’s class type and not of a class derived from the destructor’s class type (including when the destructor is invoked via a null pointer value), the program has undefined behavior. [*Note 5*: Invoking `delete` on a null pointer does not call the destructor; see [[expr.delete]](../expr/expr.delete.md). — *end note*] [*Example 1*:

```cpp
struct B {
  virtual ~B() { }
};
struct D : B {
  ~D() { }
};

D D_object;
typedef B B_alias;
B* B_ptr = &D_object;

void f() {
  D_object.B::~B();             // calls B's destructor
  B_ptr->~B();                  // calls D's destructor
  B_ptr->~B_alias();            // calls D's destructor
  B_ptr->B_alias::~B();         // calls B's destructor
  B_ptr->B_alias::~B_alias();   // calls B's destructor
}
```

— *end example*] [*Note 6*: An explicit destructor call must always be written using a member access operator ([[expr.ref]](../expr/expr.ref.md)) or a *qualified-id* ([[expr.prim.id.qual]](../expr/expr.prim.id.qual.md)); in particular, the *unary-expression* `~X()` in a member function is not an explicit destructor call ([[expr.unary.op]](../expr/expr.unary.op.md)). — *end note*]

**18** [*Note 7*: Explicit calls of destructors are rarely needed. One use of such calls is for objects placed at specific addresses using a placement *new-expression*. Such use of explicit placement and destruction of objects can be necessary to cope with dedicated hardware resources and for writing memory management facilities. For example,

```cpp
void* operator new(std::size_t, void* p) { return p; }
struct X {
  X(int);
  ~X();
};
void f(X* p);

void g() {                      // rare, specialized use:
  char* buf = new char[sizeof(X)];
  X* p = new(buf) X(222);       // use buf[] and initialize
  f(p);
  p->X::~X();                   // cleanup
}
```

— *end note*]

**19** Once a destructor is invoked for an object, the object no longer exists; the behavior is undefined if the destructor is invoked for an object whose lifetime has ended ([[basic.life]](../basic/basic.life.md)). [*Example 2*: If the destructor for an object with automatic storage duration is explicitly invoked, and the block is subsequently left in a manner that would ordinarily invoke implicit destruction of the object, the behavior is undefined. — *end example*]

**20** [*Note 8*: The notation for explicit call of a destructor can be used for any scalar type name ([[expr.prim.id.dtor]](../expr/expr.prim.id.dtor.md)). Allowing this makes it possible to write code without having to know if a destructor exists for a given type. For example:

```cpp
typedef int I;
I* p;
p->I::~I();
```

— *end note*]

**21** A destructor shall not be a coroutine.
