# 20.11.1 Class template `unique_ptr` [unique.ptr]

[Contents](../../README.md) · up: [20.11 Smart pointers \[smartptr\]](smartptr.md) · prev: [20.11 Smart pointers \[smartptr\]](smartptr.md) · next: [20.11.1.1 Default deleters \[unique.ptr.dltr\]](unique.ptr.dltr.md)

**1** A *unique pointer* is an object that owns another object and manages that other object through a pointer. More precisely, a unique pointer is an object *u* that stores a pointer to a second object *p* and will dispose of *p* when *u* is itself destroyed (e.g., when leaving block scope ([[stmt.dcl]](../../stmt/stmt.dcl.md))). In this context, *u* is said to *own* `p`.

**2** The mechanism by which *u* disposes of *p* is known as *p*’s associated *deleter*, a function object whose correct invocation results in *p*’s appropriate disposition (typically its deletion).

**3** Let the notation *u.p* denote the pointer stored by *u*, and let *u.d* denote the associated deleter. Upon request, *u* can *reset* (replace) *u.p* and *u.d* with another pointer and deleter, but properly disposes of its owned object via the associated deleter before such replacement is considered completed.

**4** Each object of a type `U` instantiated from the `unique_ptr` template specified in this subclause has the strict ownership semantics, specified above, of a unique pointer. In partial satisfaction of these semantics, each such `U` is *Cpp17MoveConstructible* and *Cpp17MoveAssignable*, but is not *Cpp17CopyConstructible* nor *Cpp17CopyAssignable*. The template parameter `T` of `unique_ptr` may be an incomplete type.

**5** [*Note 1*: The uses of `unique_ptr` include providing exception safety for dynamically allocated memory, passing ownership of dynamically allocated memory to a function, and returning dynamically allocated memory from a function. — *end note*]

## Subclauses

- [20.11.1.1 Default deleters \[unique.ptr.dltr\]](unique.ptr.dltr.md)
  - [20.11.1.1.1 In general \[unique.ptr.dltr.general\]](unique.ptr.dltr.general.md)
  - [20.11.1.1.2 `default_delete` \[unique.ptr.dltr.dflt\]](unique.ptr.dltr.dflt.md)
  - [20.11.1.1.3 `default_delete<T[]>` \[unique.ptr.dltr.dflt1\]](unique.ptr.dltr.dflt1.md)
- [20.11.1.2 `unique_ptr` for single objects \[unique.ptr.single\]](unique.ptr.single.md)
  - [20.11.1.2.1 Constructors \[unique.ptr.single.ctor\]](unique.ptr.single.ctor.md)
  - [20.11.1.2.2 Destructor \[unique.ptr.single.dtor\]](unique.ptr.single.dtor.md)
  - [20.11.1.2.3 Assignment \[unique.ptr.single.asgn\]](unique.ptr.single.asgn.md)
  - [20.11.1.2.4 Observers \[unique.ptr.single.observers\]](unique.ptr.single.observers.md)
  - [20.11.1.2.5 Modifiers \[unique.ptr.single.modifiers\]](unique.ptr.single.modifiers.md)
- [20.11.1.3 `unique_ptr` for array objects with a runtime length \[unique.ptr.runtime\]](unique.ptr.runtime.md)
  - [20.11.1.3.1 Constructors \[unique.ptr.runtime.ctor\]](unique.ptr.runtime.ctor.md)
  - [20.11.1.3.2 Assignment \[unique.ptr.runtime.asgn\]](unique.ptr.runtime.asgn.md)
  - [20.11.1.3.3 Observers \[unique.ptr.runtime.observers\]](unique.ptr.runtime.observers.md)
  - [20.11.1.3.4 Modifiers \[unique.ptr.runtime.modifiers\]](unique.ptr.runtime.modifiers.md)
- [20.11.1.4 Creation \[unique.ptr.create\]](unique.ptr.create.md)
- [20.11.1.5 Specialized algorithms \[unique.ptr.special\]](unique.ptr.special.md)
- [20.11.1.6 I/O \[unique.ptr.io\]](unique.ptr.io.md)
