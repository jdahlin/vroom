# 26.6.8.1 In general [rand.dist.general]

[Contents](../README.md) · up: [26.6.8 Random number distribution class templates \[rand.dist\]](rand.dist.md) · prev: [26.6.8 Random number distribution class templates \[rand.dist\]](rand.dist.md) · next: [26.6.8.2 Uniform distributions \[rand.dist.uni\]](rand.dist.uni.md)

**1** Each type instantiated from a class template specified in this subclause [[rand.dist]](rand.dist.md) meets the requirements of a random number distribution ([[rand.req.dist]](rand.req.dist.md)) type.

**2** Descriptions are provided in this subclause [[rand.dist]](rand.dist.md) only for distribution operations that are not described in [[rand.req.dist]](rand.req.dist.md) or for operations where there is additional semantic information. In particular, declarations for copy constructors, for copy assignment operators, for streaming operators, and for equality and inequality operators are not shown in the synopses.

**3** The algorithms for producing each of the specified distributions are implementation-defined.

**4** The value of each probability density function *p*(*z*) and of each discrete probability function *P*(*z*<sub>*i*</sub>) specified in this subclause is 0 everywhere outside its stated domain.
