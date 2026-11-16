# 26.4 Complex numbers [complex.numbers]

[Contents](../../README.md) · up: [26 Numerics library \[numerics\]](numerics.md) · prev: [26.3.1 Header `<cfenv>` synopsis \[cfenv.syn\]](cfenv.syn.md) · next: [26.4.1 Header `<complex>` synopsis \[complex.syn\]](complex.syn.md)

**1** The header `<complex>` defines a class template, and numerous functions for representing and manipulating complex numbers.

**2** The effect of instantiating the template `complex` for any type other than `float`, `double`, or `long double` is unspecified. The specializations `complex<float>`, `complex<double>`, and `complex<long double>` are literal types ([[basic.types]](../../basic/basic.types.md)).

**3** If the result of a function is not mathematically defined or not in the range of representable values for its type, the behavior is undefined.

**4** If `z` is an lvalue of type *cv* `complex<T>` then:

- the expression `reinterpret_cast<cv T(&)[2]>(z)` is well-formed,
- `reinterpret_cast<cv T(&)[2]>(z)[0]` designates the real part of `z`, and
- `reinterpret_cast<cv T(&)[2]>(z)[1]` designates the imaginary part of `z`.

Moreover, if `a` is an expression of type *cv* `complex<T>*` and the expression `a[i]` is well-defined for an integer expression `i`, then:

- `reinterpret_cast<cv T*>(a)[2*i]` designates the real part of `a[i]`, and
- `reinterpret_cast<cv T*>(a)[2*i + 1]` designates the imaginary part of `a[i]`.

## Subclauses

- [26.4.1 Header `<complex>` synopsis \[complex.syn\]](complex.syn.md)
- [26.4.2 Class template `complex` \[complex\]](complex.md)
- [26.4.3 Specializations \[complex.special\]](complex.special.md)
- [26.4.4 Member functions \[complex.members\]](complex.members.md)
- [26.4.5 Member operators \[complex.member.ops\]](complex.member.ops.md)
- [26.4.6 Non-member operations \[complex.ops\]](complex.ops.md)
- [26.4.7 Value operations \[complex.value.ops\]](complex.value.ops.md)
- [26.4.8 Transcendentals \[complex.transcendentals\]](complex.transcendentals.md)
- [26.4.9 Additional overloads \[cmplx.over\]](cmplx.over.md)
- [26.4.10 Suffixes for complex number literals \[complex.literals\]](complex.literals.md)
