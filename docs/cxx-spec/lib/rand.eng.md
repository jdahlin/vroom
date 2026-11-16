# 26.6.3 Random number engine class templates [rand.eng]

[Contents](../README.md) · up: [26.6 Random number generation \[rand\]](rand.md) · prev: [26.6.2.6 Random number distribution requirements \[rand.req.dist\]](rand.req.dist.md) · next: [26.6.3.1 Class template `linear_congruential_engine` \[rand.eng.lcong\]](rand.eng.lcong.md)

**1** Each type instantiated from a class template specified in this subclause [[rand.eng]](rand.eng.md) meets the requirements of a random number engine ([[rand.req.eng]](rand.req.eng.md)) type.

**2** Except where specified otherwise, the complexity of each function specified in this subclause [[rand.eng]](rand.eng.md) is constant.

**3** Except where specified otherwise, no function described in this subclause [[rand.eng]](rand.eng.md) throws an exception.

**4** Every function described in this subclause [[rand.eng]](rand.eng.md) that has a function parameter `q` of type `Sseq&` for a template type parameter named `Sseq` that is different from type `seed_seq` throws what and when the invocation of `q.generate` throws.

**5** Descriptions are provided in this subclause [[rand.eng]](rand.eng.md) only for engine operations that are not described in [[rand.req.eng]](rand.req.eng.md) or for operations where there is additional semantic information. In particular, declarations for copy constructors, for copy assignment operators, for streaming operators, and for equality and inequality operators are not shown in the synopses.

**6** Each template specified in this subclause [[rand.eng]](rand.eng.md) requires one or more relationships, involving the value(s) of its non-type template parameter(s), to hold. A program instantiating any of these templates is ill-formed if any such required relationship fails to hold.

**7** For every random number engine and for every random number engine adaptor `X` defined in this subclause ([[rand.eng]](rand.eng.md)) and in subclause [[rand.adapt]](rand.adapt.md):

- if the constructor

  ```cpp
  template<class Sseq> explicit X(Sseq& q);
  ```

  is called with a type `Sseq` that does not qualify as a seed sequence, then this constructor shall not participate in overload resolution;

- if the member function

  ```cpp
  template<class Sseq> void seed(Sseq& q);
  ```

  is called with a type `Sseq` that does not qualify as a seed sequence, then this function shall not participate in overload resolution.

The extent to which an implementation determines that a type cannot be a seed sequence is unspecified, except that as a minimum a type shall not qualify as a seed sequence if it is implicitly convertible to `X::result_type`.

## Subclauses

- [26.6.3.1 Class template `linear_congruential_engine` \[rand.eng.lcong\]](rand.eng.lcong.md)
- [26.6.3.2 Class template `mersenne_twister_engine` \[rand.eng.mers\]](rand.eng.mers.md)
- [26.6.3.3 Class template `subtract_with_carry_engine` \[rand.eng.sub\]](rand.eng.sub.md)
