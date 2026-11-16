# 9.4.1 Aggregates [dcl.init.aggr]

[Contents](README.md) · up: [9.4 Initializers \[dcl.init\]](dcl.init.md) · prev: [9.4 Initializers \[dcl.init\]](dcl.init.md) · next: [9.4.2 Character arrays \[dcl.init.string\]](dcl.init.string.md)

**1** An *aggregate* is an array or a class ([[class]](class.md)) with

- no user-declared or inherited constructors ([[class.ctor]](class.ctor.md)),
- no private or protected direct non-static data members ([[class.access]](class.access.md)),
- no virtual functions ([[class.virtual]](class.virtual.md)), and
- no virtual, private, or protected base classes ([[class.mi]](class.mi.md)).

[*Note 1*: Aggregate initialization does not allow accessing protected and private base class’ members or constructors. — *end note*]

**2** The *elements* of an aggregate are:

- for an array, the array elements in increasing subscript order, or
- for a class, the direct base classes in declaration order, followed by the direct non-static data members ([[class.mem]](class.mem.md)) that are not members of an anonymous union, in declaration order.

**3** When an aggregate is initialized by an initializer list as specified in [[dcl.init.list]](dcl.init.list.md), the elements of the initializer list are taken as initializers for the elements of the aggregate. The *explicitly initialized elements* of the aggregate are determined as follows:

- If the initializer list is a *designated-initializer-list*, the aggregate shall be of class type, the *identifier* in each *designator* shall name a direct non-static data member of the class, and the explicitly initialized elements of the aggregate are the elements that are, or contain, those members.
- If the initializer list is an *initializer-list*, the explicitly initialized elements of the aggregate are the first *n* elements of the aggregate, where *n* is the number of elements in the initializer list.
- Otherwise, the initializer list must be `{}`, and there are no explicitly initialized elements.

**4** For each explicitly initialized element:

- If the element is an anonymous union object and the initializer list is a *designated-initializer-list*, the anonymous union object is initialized by the *designated-initializer-list* `{`*D*`}`, where *D* is the *designated-initializer-clause* naming a member of the anonymous union object. There shall be only one such *designated-initializer-clause*. [*Example 1*:

  ```cpp
  struct C {
    union {
      int a;
      const char* p;
    };
    int x;
  } c = { .a = 1, .x = 3 };
  ```

  initializes `c.a` with 1 and `c.x` with 3. — *end example*]

- Otherwise, the element is copy-initialized from the corresponding *initializer-clause* or is initialized with the *brace-or-equal-initializer* of the corresponding *designated-initializer-clause*. If that initializer is of the form *assignment-expression* or `=`*assignment-expression* and a narrowing conversion ([[dcl.init.list]](dcl.init.list.md)) is required to convert the expression, the program is ill-formed. [*Note 2*: If an initializer is itself an initializer list, the element is list-initialized, which will result in a recursive application of the rules in this subclause if the element is an aggregate. — *end note*] [*Example 2*:

  ```cpp
  struct A {
    int x;
    struct B {
      int i;
      int j;
    } b;
  } a = { 1, { 2, 3 } };
  ```

  initializes `a.x` with 1, `a.b.i` with 2, `a.b.j` with 3.

  ```cpp
  struct base1 { int b1, b2 = 42; };
  struct base2 {
    base2() {
      b3 = 42;
    }
    int b3;
  };
  struct derived : base1, base2 {
    int d;
  };

  derived d1{{1, 2}, {}, 4};
  derived d2{{}, {}, 4};
  ```

  initializes `d1.b1` with 1, `d1.b2` with 2, `d1.b3` with 42, `d1.d` with 4, and `d2.b1` with 0, `d2.b2` with 42, `d2.b3` with 42, `d2.d` with 4. — *end example*]

**5** For a non-union aggregate, each element that is not an explicitly initialized element is initialized as follows:

- If the element has a default member initializer ([[class.mem]](class.mem.md)), the element is initialized from that initializer.
- Otherwise, if the element is not a reference, the element is copy-initialized from an empty initializer list ([[dcl.init.list]](dcl.init.list.md)).
- Otherwise, the program is ill-formed.

If the aggregate is a union and the initializer list is empty, then

- if any variant member has a default member initializer, that member is initialized from its default member initializer;
- otherwise, the first member of the union (if any) is copy-initialized from an empty initializer list.

[*Example 3*:

```cpp
struct S { int a; const char* b; int c; int d = b[a]; };
S ss = { 1, "asdf" };
```

initializes `ss.a` with 1, `ss.b` with `"asdf"`, `ss.c` with the value of an expression of the form `int{}` (that is, `0`), and `ss.d` with the value of `ss.b[ss.a]` (that is, `'s'`), and in

```cpp
struct X { int i, j, k = 42; };
X a[] = { 1, 2, 3, 4, 5, 6 };
X b[2] = { { 1, 2, 3 }, { 4, 5, 6 } };
```

`a` and `b` have the same value

```cpp
struct A {
  string a;
  int b = 42;
  int c = -1;
};
```

`A{.c=21}` has the following steps:

- Initialize `a` with `{}`
- Initialize `b` with `= 42`
- Initialize `c` with `= 21`

— *end example*]

**6** The initializations of the elements of the aggregate are evaluated in the element order. That is, all value computations and side effects associated with a given element are sequenced before those of any element that follows it in order.

**7** An aggregate that is a class can also be initialized with a single expression not enclosed in braces, as described in [[dcl.init]](dcl.init.md).

**8** The destructor for each element of class type is potentially invoked ([[class.dtor]](class.dtor.md)) from the context where the aggregate initialization occurs. [*Note 3*: This provision ensures that destructors can be called for fully-constructed subobjects in case an exception is thrown ([[except.ctor]](except.ctor.md)). — *end note*]

**9** An array of unknown bound initialized with a brace-enclosed *initializer-list* containing `n` *initializer-clause*s is defined as having `n` elements ([[dcl.array]](dcl.array.md)). [*Example 4*:

```cpp
int x[] = { 1, 3, 5 };
```

declares and initializes `x` as a one-dimensional array that has three elements since no size was specified and there are three initializers. — *end example*] An array of unknown bound shall not be initialized with an empty *braced-init-list* `{}`.[^1] [*Note 4*: A default member initializer does not determine the bound for a member array of unknown bound. Since the default member initializer is ignored if a suitable *mem-initializer* is present ([[class.base.init]](class.base.init.md)), the default member initializer is not considered to initialize the array of unknown bound. [*Example 5*:

```cpp
struct S {
  int y[] = { 0 };          // error: non-static data member of incomplete type
};
```

— *end example*] — *end note*]

**10** [*Note 5*: Static data members, non-static data members of anonymous union members, and unnamed bit-fields are not considered elements of the aggregate. [*Example 6*:

```cpp
struct A {
  int i;
  static int s;
  int j;
  int :17;
  int k;
} a = { 1, 2, 3 };
```

Here, the second initializer 2 initializes `a.j` and not the static data member `A::s`, and the third initializer 3 initializes `a.k` and not the unnamed bit-field before it. — *end example*] — *end note*]

**11** An *initializer-list* is ill-formed if the number of *initializer-clause*s exceeds the number of elements of the aggregate. [*Example 7*:

```cpp
char cv[4] = { 'a', 's', 'd', 'f', 0 };     // error
```

is ill-formed. — *end example*]

**12** If a member has a default member initializer and a potentially-evaluated subexpression thereof is an aggregate initialization that would use that default member initializer, the program is ill-formed. [*Example 8*:

```cpp
struct A;
extern A a;
struct A {
  const A& a1 { A{a,a} };       // OK
  const A& a2 { A{} };          // error
};
A a{a,a};                       // OK

struct B {
  int n = B{}.n;                // error
};
```

— *end example*]

**13** If an aggregate class `C` contains a subaggregate element `e` with no elements, the *initializer-clause* for `e` shall not be omitted from an *initializer-list* for an object of type `C` unless the *initializer-clause*s for all elements of `C` following `e` are also omitted. [*Example 9*:

```cpp
struct S { } s;
struct A {
  S s1;
  int i1;
  S s2;
  int i2;
  S s3;
  int i3;
} a = {
  { },              // Required initialization
  0,
  s,                // Required initialization
  0
};                  // Initialization not required for A::s3 because A::i3 is also not initialized
```

— *end example*]

**14** When initializing a multi-dimensional array, the *initializer-clause*s initialize the elements with the last (rightmost) index of the array varying the fastest ([[dcl.array]](dcl.array.md)). [*Example 10*:

```cpp
int x[2][2] = { 3, 1, 4, 2 };
```

initializes `x[0][0]` to `3`, `x[0][1]` to `1`, `x[1][0]` to `4`, and `x[1][1]` to `2`. On the other hand,

```cpp
float y[4][3] = {
  { 1 }, { 2 }, { 3 }, { 4 }
};
```

initializes the first column of `y` (regarded as a two-dimensional array) and leaves the rest zero. — *end example*]

**15** Braces can be elided in an *initializer-list* as follows. If the *initializer-list* begins with a left brace, then the succeeding comma-separated list of *initializer-clause*s initializes the elements of a subaggregate; it is erroneous for there to be more *initializer-clause*s than elements. If, however, the *initializer-list* for a subaggregate does not begin with a left brace, then only enough *initializer-clause*s from the list are taken to initialize the elements of the subaggregate; any remaining *initializer-clause*s are left to initialize the next element of the aggregate of which the current subaggregate is an element. [*Example 11*:

```cpp
float y[4][3] = {
  { 1, 3, 5 },
  { 2, 4, 6 },
  { 3, 5, 7 },
};
```

is a completely-braced initialization: 1, 3, and 5 initialize the first row of the array `y[0]`, namely `y[0][0]`, `y[0][1]`, and `y[0][2]`. Likewise the next two lines initialize `y[1]` and `y[2]`. The initializer ends early and therefore `y[3]`s elements are initialized as if explicitly initialized with an expression of the form `float()`, that is, are initialized with `0.0`. In the following example, braces in the *initializer-list* are elided; however the *initializer-list* has the same effect as the completely-braced *initializer-list* of the above example,

```cpp
float y[4][3] = {
  1, 3, 5, 2, 4, 6, 3, 5, 7
};
```

The initializer for `y` begins with a left brace, but the one for `y[0]` does not, therefore three elements from the list are used. Likewise the next three are taken successively for `y[1]` and `y[2]`. — *end example*]

**16** All implicit type conversions ([[conv]](conv.md)) are considered when initializing the element with an *assignment-expression*. If the *assignment-expression* can initialize an element, the element is initialized. Otherwise, if the element is itself a subaggregate, brace elision is assumed and the *assignment-expression* is considered for the initialization of the first element of the subaggregate. [*Note 6*: As specified above, brace elision cannot apply to subaggregates with no elements; an *initializer-clause* for the entire subobject is required. — *end note*]

[*Example 12*:

```cpp
struct A {
  int i;
  operator int();
};
struct B {
  A a1, a2;
  int z;
};
A a;
B b = { 4, a, a };
```

Braces are elided around the *initializer-clause* for `b.a1.i`. `b.a1.i` is initialized with 4, `b.a2` is initialized with `a`, `b.z` is initialized with whatever `a.operator int()` returns. — *end example*]

**17** [*Note 7*: An aggregate array or an aggregate class may contain elements of a class type with a user-declared constructor ([[class.ctor]](class.ctor.md)). Initialization of these aggregate objects is described in [[class.expl.init]](class.expl.init.md). — *end note*]

**18** [*Note 8*: Whether the initialization of aggregates with static storage duration is static or dynamic is specified in [[basic.start.static]](basic.start.static.md), [[basic.start.dynamic]](basic.start.dynamic.md), and [[stmt.dcl]](stmt.dcl.md). — *end note*]

**19** When a union is initialized with an initializer list, there shall not be more than one explicitly initialized element. [*Example 13*:

```cpp
union u { int a; const char* b; };
u a = { 1 };
u b = a;
u c = 1;                        // error
u d = { 0, "asdf" };            // error
u e = { "asdf" };               // error
u f = { .b = "asdf" };
u g = { .a = 1, .b = "asdf" };  // error
```

— *end example*]

**20** [*Note 9*: As described above, the braces around the *initializer-clause* for a union member can be omitted if the union is a member of another aggregate. — *end note*]

[^1]: The syntax provides for empty *braced-init-list*s, but nonetheless C++ does not have zero length arrays.
