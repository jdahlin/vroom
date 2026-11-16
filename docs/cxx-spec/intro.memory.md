# 6.7.1 Memory model [intro.memory]

[Contents](README.md) · up: [6.7 Memory and objects \[basic.memobj\]](basic.memobj.md) · prev: [6.7 Memory and objects \[basic.memobj\]](basic.memobj.md) · next: [6.7.2 Object model \[intro.object\]](intro.object.md)

**1** The fundamental storage unit in the C++ memory model is the *byte*. A byte is at least large enough to contain any member of the basic execution character set ([[lex.charset]](lex.charset.md)) and the eight-bit code units of the Unicode UTF-8 encoding form and is composed of a contiguous sequence of bits,[^1] the number of which is implementation-defined. The least significant bit is called the *low-order bit*; the most significant bit is called the *high-order bit*. The memory available to a C++ program consists of one or more sequences of contiguous bytes. Every byte has a unique address.

**2** [*Note 1*: The representation of types is described in [[basic.types]](basic.types.md). — *end note*]

**3** A *memory location* is either an object of scalar type or a maximal sequence of adjacent bit-fields all having nonzero width. [*Note 2*: Various features of the language, such as references and virtual functions, might involve additional memory locations that are not accessible to programs but are managed by the implementation. — *end note*] Two or more threads of execution ([[intro.multithread]](intro.multithread.md)) can access separate memory locations without interfering with each other.

**4** [*Note 3*: Thus a bit-field and an adjacent non-bit-field are in separate memory locations, and therefore can be concurrently updated by two threads of execution without interference. The same applies to two bit-fields, if one is declared inside a nested struct declaration and the other is not, or if the two are separated by a zero-length bit-field declaration, or if they are separated by a non-bit-field declaration. It is not safe to concurrently update two bit-fields in the same struct if all fields between them are also bit-fields of nonzero width. — *end note*]

**5** [*Example 1*: A class declared as

```cpp
struct {
  char a;
  int b:5,
  c:11,
  :0,
  d:8;
  struct {int ee:8;} e;
}
```

contains four separate memory locations: The member `a` and bit-fields `d` and `e.ee` are each separate memory locations, and can be modified concurrently without interfering with each other. The bit-fields `b` and `c` together constitute the fourth memory location. The bit-fields `b` and `c` cannot be concurrently modified, but `b` and `a`, for example, can be. — *end example*]

[^1]: The number of bits in a byte is reported by the macro `CHAR_BIT` in the header `<climits>` ([[climits.syn]](lib/climits.syn.md)).
