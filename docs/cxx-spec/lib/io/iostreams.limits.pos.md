# 29.2.2 Positioning type limitations [iostreams.limits.pos]

[Contents](../../README.md) · up: [29.2 Iostreams requirements \[iostreams.requirements\]](iostreams.requirements.md) · prev: [29.2.1 Imbue limitations \[iostream.limits.imbue\]](iostream.limits.imbue.md) · next: [29.2.3 Thread safety \[iostreams.threadsafety\]](iostreams.threadsafety.md)

**1** The classes of [[input.output]](input.output.md) with template arguments `charT` and `traits` behave as described if `traits::pos_type` and `traits::off_type` are `streampos` and `streamoff` respectively. Except as noted explicitly below, their behavior when `traits::pos_type` and `traits::off_type` are other types is implementation-defined.

**2** In the classes of [[input.output]](input.output.md), a template parameter with name `charT` represents a member of the set of types containing `char`, `wchar_t`, and any other implementation-defined character types that meet the requirements for a character on which any of the iostream components can be instantiated.
