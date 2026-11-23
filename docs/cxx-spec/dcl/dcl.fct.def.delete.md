# 9.5.3 Deleted definitions [dcl.fct.def.delete]

[Contents](../README.md) · up: [9.5 Function definitions \[dcl.fct.def\]](dcl.fct.def.md) · prev: [9.5.2 Explicitly-defaulted functions \[dcl.fct.def.default\]](dcl.fct.def.default.md) · next: [9.5.4 Coroutine definitions \[dcl.fct.def.coroutine\]](dcl.fct.def.coroutine.md)

**1** A function definition whose *function-body* is of the form `= delete;` is called a *deleted definition*. A function with a deleted definition is also called a *deleted function*.

**2** A program that refers to a deleted function implicitly or explicitly, other than to declare it, is ill-formed. [*Note 1*: This includes calling the function implicitly or explicitly and forming a pointer or pointer-to-member to the function. It applies even for references in expressions that are not potentially-evaluated. If a function is overloaded, it is referenced only if the function is selected by overload resolution. The implicit odr-use ([[basic.def.odr]](../basic/basic.def.odr.md)) of a virtual function does not, by itself, constitute a reference. — *end note*]

**3** [*Example 1*: One can prevent default initialization and initialization by non-`double`s with

```cpp
struct onlydouble {
  onlydouble() = delete;                // OK, but redundant
  template<class T>
    onlydouble(T) = delete;
  onlydouble(double);
};
```

— *end example*]

[*Example 2*: One can prevent use of a class in certain *new-expression*s by using deleted definitions of a user-declared `operator new` for that class.

```cpp
struct sometype {
  void* operator new(std::size_t) = delete;
  void* operator new[](std::size_t) = delete;
};
sometype* p = new sometype;     // error: deleted class operator new
sometype* q = new sometype[3];  // error: deleted class operator new[]
```

— *end example*]

[*Example 3*: One can make a class uncopyable, i.e., move-only, by using deleted definitions of the copy constructor and copy assignment operator, and then providing defaulted definitions of the move constructor and move assignment operator.

```cpp
struct moveonly {
  moveonly() = default;
  moveonly(const moveonly&) = delete;
  moveonly(moveonly&&) = default;
  moveonly& operator=(const moveonly&) = delete;
  moveonly& operator=(moveonly&&) = default;
  ~moveonly() = default;
};
moveonly* p;
moveonly q(*p);                 // error: deleted copy constructor
```

— *end example*]

**4** A deleted function is implicitly an inline function ([[dcl.inline]](dcl.inline.md)). [*Note 2*: The one-definition rule ([[basic.def.odr]](../basic/basic.def.odr.md)) applies to deleted definitions. — *end note*] A deleted definition of a function shall be the first declaration of the function or, for an explicit specialization of a function template, the first declaration of that specialization. An implicitly declared allocation or deallocation function ([[basic.stc.dynamic]](../basic/basic.stc.dynamic.md)) shall not be defined as deleted. [*Example 4*:

```cpp
struct sometype {
  sometype();
};
sometype::sometype() = delete;  // error: not first declaration
```

— *end example*]
