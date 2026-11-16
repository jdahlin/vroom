# C.1.3 [[expr]](expr.md): expressions [diff.cpp17.expr]

[Contents](README.md) · up: [C.1 C++ and ISO C++ 2017 \[diff.cpp17\]](diff.cpp17.md) · prev: [C.1.2 [[basic]](basic.md): basics \[diff.cpp17.basic\]](diff.cpp17.basic.md) · next: [C.1.4 [[dcl.dcl]](dcl.dcl.md): declarations \[diff.cpp17.dcl.dcl\]](diff.cpp17.dcl.dcl.md)

**1** **Affected subclause:** [[expr.prim.lambda.capture]](expr.prim.lambda.capture.md) <br>**Change:** Implicit lambda capture may capture additional entities. <br>**Rationale:** Rule simplification, necessary to resolve interactions with constexpr if. <br>**Effect on original feature:** Lambdas with a *capture-default* may capture local entities that were not captured in C++ 2017 if those entities are only referenced in contexts that do not result in an odr-use.
