# 11.4.9 Bit-fields [class.bit]

[Contents](../README.md) · up: [11.4 Class members \[class.mem\]](class.mem.md) · prev: [11.4.8.2 Static data members \[class.static.data\]](class.static.data.md) · next: [11.4.10 Nested class declarations \[class.nest\]](class.nest.md)

**1** A *member-declarator* of the form

```bnf
identifierₒₚₜ attribute-specifier-seqₒₚₜ ':' constant-expression brace-or-equal-initializerₒₚₜ
```

specifies a bit-field. The optional *attribute-specifier-seq* appertains to the entity being declared. A bit-field shall not be a static member. A bit-field shall have integral or enumeration type; the bit-field semantic property is not part of the type of the class member. The *constant-expression* shall be an integral constant expression with a value greater than or equal to zero and is called the *width* of the bit-field. If the width of a bit-field is larger than the width of the bit-field’s type (or, in case of an enumeration type, of its underlying type), the extra bits are padding bits ([[basic.types]](../basic/basic.types.md)). Allocation of bit-fields within a class object is implementation-defined. Alignment of bit-fields is implementation-defined. Bit-fields are packed into some addressable allocation unit. [*Note 1*: Bit-fields straddle allocation units on some machines and not on others. Bit-fields are assigned right-to-left on some machines, left-to-right on others. — *end note*]

**2** A declaration for a bit-field that omits the *identifier* declares an *unnamed bit-field*. Unnamed bit-fields are not members and cannot be initialized. An unnamed bit-field shall not be declared with a cv-qualified type. [*Note 2*: An unnamed bit-field is useful for padding to conform to externally-imposed layouts. — *end note*] As a special case, an unnamed bit-field with a width of zero specifies alignment of the next bit-field at an allocation unit boundary. Only when declaring an unnamed bit-field may the width be zero.

**3** The address-of operator `&` shall not be applied to a bit-field, so there are no pointers to bit-fields. A non-const reference shall not be bound to a bit-field ([[dcl.init.ref]](../dcl/dcl.init.ref.md)). [*Note 3*: If the initializer for a reference of type `const` `T&` is an lvalue that refers to a bit-field, the reference is bound to a temporary initialized to hold the value of the bit-field; the reference is not bound to the bit-field directly. See [[dcl.init.ref]](../dcl/dcl.init.ref.md). — *end note*]

**4** If a value of integral type (other than `bool`) is stored into a bit-field of width *N* and the value would be representable in a hypothetical signed or unsigned integer type with width *N* and the same signedness as the bit-field’s type, the original value and the value of the bit-field compare equal. If the value `true` or `false` is stored into a bit-field of type `bool` of any size (including a one bit bit-field), the original `bool` value and the value of the bit-field compare equal. If a value of an enumeration type is stored into a bit-field of the same type and the width is large enough to hold all the values of that enumeration type ([[dcl.enum]](../dcl/dcl.enum.md)), the original value and the value of the bit-field compare equal. [*Example 1*:

```cpp
enum BOOL { FALSE=0, TRUE=1 };
struct A {
  BOOL b:1;
};
A a;
void f() {
  a.b = TRUE;
  if (a.b == TRUE)              // yields true
    { /* ... */ }
}
```

— *end example*]
