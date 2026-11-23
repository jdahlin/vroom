# 16.5.5.4 Non-member functions [global.functions]

[Contents](../../README.md) · up: [16.5.5 Conforming implementations \[conforming\]](conforming.md) · prev: [16.5.5.3 Restrictions on macro definitions \[res.on.macro.definitions\]](res.on.macro.definitions.md) · next: [16.5.5.5 Member functions \[member.functions\]](member.functions.md)

**1** It is unspecified whether any non-member functions in the C++ standard library are defined as inline ([[dcl.inline]](../../dcl/dcl.inline.md)).

**2** A call to a non-member function signature described in [[support]](../support/support.md) through [[thread]](../thread/thread.md) and [[depr]](../../depr/depr.md) shall behave as if the implementation declared no additional non-member function signatures.[^1]

**3** An implementation shall not declare a non-member function signature with additional default arguments.

**4** Unless otherwise specified, calls made by functions in the standard library to non-operator, non-member functions do not use functions from another namespace which are found through argument-dependent name lookup ([[basic.lookup.argdep]](../../basic/basic.lookup.argdep.md)). [*Note 1*: The phrase “unless otherwise specified” applies to cases such as the swappable with requirements ([[swappable.requirements]](swappable.requirements.md)). The exception for overloaded operators allows argument-dependent lookup in cases like that of `ostream_iterator::operator=` ([[ostream.iterator.ops]](../iterators/ostream.iterator.ops.md)):

*Effects:*

```cpp
*out_stream << value;
if (delim != 0)
  *out_stream << delim;
return *this;
```

— *end note*]

[^1]: A valid C++ program always calls the expected library non-member function. An implementation may also define additional non-member functions that would otherwise not be called by a valid C++ program.
