# 26.6.4.1 In general [rand.adapt.general]

[Contents](../README.md) · up: [26.6.4 Random number engine adaptor class templates \[rand.adapt\]](rand.adapt.md) · prev: [26.6.4 Random number engine adaptor class templates \[rand.adapt\]](rand.adapt.md) · next: [26.6.4.2 Class template `discard_block_engine` \[rand.adapt.disc\]](rand.adapt.disc.md)

**1** Each type instantiated from a class template specified in this subclause [[rand.adapt]](rand.adapt.md) meets the requirements of a random number engine adaptor ([[rand.req.adapt]](rand.req.adapt.md)) type.

**2** Except where specified otherwise, the complexity of each function specified in this subclause [[rand.adapt]](rand.adapt.md) is constant.

**3** Except where specified otherwise, no function described in this subclause [[rand.adapt]](rand.adapt.md) throws an exception.

**4** Every function described in this subclause [[rand.adapt]](rand.adapt.md) that has a function parameter `q` of type `Sseq&` for a template type parameter named `Sseq` that is different from type `seed_seq` throws what and when the invocation of `q.generate` throws.

**5** Descriptions are provided in this subclause [[rand.adapt]](rand.adapt.md) only for adaptor operations that are not described in subclause [[rand.req.adapt]](rand.req.adapt.md) or for operations where there is additional semantic information. In particular, declarations for copy constructors, for copy assignment operators, for streaming operators, and for equality and inequality operators are not shown in the synopses.

**6** Each template specified in this subclause [[rand.adapt]](rand.adapt.md) requires one or more relationships, involving the value(s) of its non-type template parameter(s), to hold. A program instantiating any of these templates is ill-formed if any such required relationship fails to hold.
