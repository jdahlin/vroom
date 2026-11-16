# C.2.2 [[expr]](expr.md): expressions [diff.cpp14.expr]

[Contents](README.md) · up: [C.2 C++ and ISO C++ 2014 \[diff.cpp14\]](diff.cpp14.md) · prev: [C.2.1 [[lex]](lex.md): lexical conventions \[diff.cpp14.lex\]](diff.cpp14.lex.md) · next: [C.2.3 [[dcl.dcl]](dcl.dcl.md): declarations \[diff.cpp14.dcl.dcl\]](diff.cpp14.dcl.dcl.md)

**1** **Affected subclauses:** [[expr.post.incr]](expr.post.incr.md), [[expr.pre.incr]](expr.pre.incr.md) <br>**Change:** Remove increment operator with `bool` operand. <br>**Rationale:** Obsolete feature with occasionally surprising semantics. <br>**Effect on original feature:** A valid C++ 2014 expression utilizing the increment operator on a `bool` lvalue is ill-formed in this International Standard. Note that this might occur when the lvalue has a type given by a template parameter.

**2** **Affected subclauses:** [[expr.new]](expr.new.md), [[expr.delete]](expr.delete.md) <br>**Change:** Dynamic allocation mechanism for over-aligned types. <br>**Rationale:** Simplify use of over-aligned types. <br>**Effect on original feature:** In C++ 2014 code that uses a *new-expression* to allocate an object with an over-aligned class type, where that class has no allocation functions of its own, `::operator new(std::size_t)` is used to allocate the memory. In this International Standard, `::operator new(std::size_t, std::align_val_t)` is used instead.
