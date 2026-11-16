# 17.6.4 Pointer optimization barrier [ptr.launder]

[Contents](../README.md) · up: [17.6 Dynamic memory management \[support.dynamic\]](support.dynamic.md) · prev: [17.6.3.5 `get_new_handler` \[get.new.handler\]](get.new.handler.md) · next: [17.6.5 Hardware interference size \[hardware.interference\]](hardware.interference.md)

```cpp
template<class T> [[nodiscard]] constexpr T* launder(T* p) noexcept;
```

**1** *Mandates:* `!is_function_v<T> && !is_void_v<T>` is `true`.

**2** *Preconditions:* `p` represents the address *A* of a byte in memory. An object *X* that is within its lifetime ([[basic.life]](../basic.life.md)) and whose type is similar ([[conv.qual]](../conv.qual.md)) to `T` is located at the address *A*. All bytes of storage that would be reachable through the result are reachable through `p` (see below).

**3** *Returns:* A value of type `T*` that points to `X`.

**4** *Remarks:* An invocation of this function may be used in a core constant expression whenever the value of its argument may be used in a core constant expression. A byte of storage *b* is reachable through a pointer value that points to an object *Y* if there is an object *Z*, pointer-interconvertible with *Y*, such that *b* is within the storage occupied by *Z*, or the immediately-enclosing array object if *Z* is an array element.

**5** [*Note 1*: If a new object is created in storage occupied by an existing object of the same type, a pointer to the original object can be used to refer to the new object unless its complete object is a const object or it is a base class subobject; in the latter cases, this function can be used to obtain a usable pointer to the new object. See [[basic.life]](../basic.life.md). — *end note*]

**6** [*Example 1*:

```cpp
struct X { int n; };
const X *p = new const X{3};
const int a = p->n;
new (const_cast<X*>(p)) const X{5}; // p does not point to new object ([basic.life]) because its type is const
const int b = p->n;                 // undefined behavior
const int c = std::launder(p)->n;   // OK
```

— *end example*]
