# 26.6.2.1 General requirements [rand.req.genl]

[Contents](../../README.md) · up: [26.6.2 Requirements \[rand.req\]](rand.req.md) · prev: [26.6.2 Requirements \[rand.req\]](rand.req.md) · next: [26.6.2.2 Seed sequence requirements \[rand.req.seedseq\]](rand.req.seedseq.md)

**1** Throughout this subclause [[rand]](rand.md), the effect of instantiating a template:

- that has a template type parameter named `Sseq` is undefined unless the corresponding template argument is cv-unqualified and meets the requirements of seed sequence ([[rand.req.seedseq]](rand.req.seedseq.md)).
- that has a template type parameter named `URBG` is undefined unless the corresponding template argument is cv-unqualified and meets the requirements of uniform random bit generator ([[rand.req.urng]](rand.req.urng.md)).
- that has a template type parameter named `Engine` is undefined unless the corresponding template argument is cv-unqualified and meets the requirements of random number engine ([[rand.req.eng]](rand.req.eng.md)).
- that has a template type parameter named `RealType` is undefined unless the corresponding template argument is cv-unqualified and is one of `float`, `double`, or `long double`.
- that has a template type parameter named `IntType` is undefined unless the corresponding template argument is cv-unqualified and is one of `short`, `int`, `long`, `long long`, `unsigned short`, `unsigned int`, `unsigned long`, or `unsigned long long`.
- that has a template type parameter named `UIntType` is undefined unless the corresponding template argument is cv-unqualified and is one of `unsigned short`, `unsigned int`, `unsigned long`, or `unsigned long long`.

**2** Throughout this subclause [[rand]](rand.md), phrases of the form “`x` is an iterator of a specific kind” shall be interpreted as equivalent to the more formal requirement that “`x` is a value of a type meeting the requirements of the specified iterator type”.

**3** Throughout this subclause [[rand]](rand.md), any constructor that can be called with a single argument and that meets a requirement specified in this subclause shall be declared `explicit`.
