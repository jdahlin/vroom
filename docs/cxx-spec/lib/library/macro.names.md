# 16.5.4.3.2 Macro names [macro.names]

[Contents](../../README.md) · up: [16.5.4.3 Reserved names \[reserved.names\]](reserved.names.md) · prev: [16.5.4.3.1 Zombie names \[zombie.names\]](zombie.names.md) · next: [16.5.4.3.3 External linkage \[extern.names\]](extern.names.md)

**1** A translation unit that includes a standard library header shall not `#define` or `#undef` names declared in any standard library header.

**2** A translation unit shall not `#define` or `#undef` names lexically identical to keywords, to the identifiers listed in [Table 4](../../lex/lex.name.md#tab:lex.name.special), or to the *attribute-token*s described in [[dcl.attr]](../../dcl/dcl.attr.md), except that the names `likely` and `unlikely` may be defined as function-like macros ([[cpp.replace]](../../cpp/cpp.replace.md)).
