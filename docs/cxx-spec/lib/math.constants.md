# 26.9.2 Mathematical constants [math.constants]

[Contents](../README.md) · up: [26.9 Numbers \[numbers\]](numbers.md) · prev: [26.9.1 Header `<numbers>` synopsis \[numbers.syn\]](numbers.syn.md) · next: [27 Time library \[time\]](time.md)

**1** The library-defined partial specializations of mathematical constant variable templates are initialized with the nearest representable values of *e*, log<sub>2</sub> *e*, log<sub>10</sub> *e*, π, $\frac{1}{\pi}$, $\frac{1}{\sqrt{\pi}}$, ln 2, ln 10, $\sqrt{2}$, $\sqrt{3}$, $\frac{1}{\sqrt{3}}$, the Euler-Mascheroni γ constant, and the golden ratio φ constant $\frac{1+\sqrt{5}}{2}$, respectively.

**2** Pursuant to [[namespace.std]](namespace.std.md), a program may partially or explicitly specialize a mathematical constant variable template provided that the specialization depends on a program-defined type.

**3** A program that instantiates a primary template of a mathematical constant variable template is ill-formed.
