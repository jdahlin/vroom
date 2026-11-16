# 13.8.2.4 Dependent template arguments [temp.dep.temp]

[Contents](README.md) · up: [13.8.2 Dependent names \[temp.dep\]](temp.dep.md) · prev: [13.8.2.3 Value-dependent expressions \[temp.dep.constexpr\]](temp.dep.constexpr.md) · next: [13.8.3 Non-dependent names \[temp.nondep\]](temp.nondep.md)

**1** A type *template-argument* is dependent if the type it specifies is dependent.

**2** A non-type *template-argument* is dependent if its type is dependent or the constant expression it specifies is value-dependent.

**3** Furthermore, a non-type *template-argument* is dependent if the corresponding non-type *template-parameter* is of reference or pointer type and the *template-argument* designates or points to a member of the current instantiation or a member of a dependent type.

**4** A template *template-argument* is dependent if it names a *template-parameter* or is a *qualified-id* that refers to a member of an unknown specialization.
