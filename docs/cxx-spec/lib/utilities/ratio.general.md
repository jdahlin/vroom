# 20.16.1 In general [ratio.general]

[Contents](../../README.md) · up: [20.16 Compile-time rational arithmetic \[ratio\]](ratio.md) · prev: [20.16 Compile-time rational arithmetic \[ratio\]](ratio.md) · next: [20.16.2 Header `<ratio>` synopsis \[ratio.syn\]](ratio.syn.md)

**1** Subclause [[ratio]](ratio.md) describes the ratio library. It provides a class template `ratio` which exactly represents any finite rational number with a numerator and denominator representable by compile-time constants of type `intmax_t`.

**2** Throughout subclause [[ratio]](ratio.md), the names of template parameters are used to express type requirements. If a template parameter is named `R1` or `R2`, and the template argument is not a specialization of the `ratio` template, the program is ill-formed.
