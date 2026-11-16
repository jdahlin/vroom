# 26.6 Random number generation [rand]

[Contents](../../README.md) · up: [26 Numerics library \[numerics\]](numerics.md) · prev: [26.5.7 Endian \[bit.endian\]](bit.endian.md) · next: [26.6.1 Header `<random>` synopsis \[rand.synopsis\]](rand.synopsis.md)

**1** This subclause defines a facility for generating (pseudo-)random numbers.

**2** In addition to a few utilities, four categories of entities are described: *uniform random bit generators*, *random number engines*, *random number engine adaptors*, and *random number distributions*. These categorizations are applicable to types that meet the corresponding requirements, to objects instantiated from such types, and to templates producing such types when instantiated. [*Note 1*: These entities are specified in such a way as to permit the binding of any uniform random bit generator object `e` as the argument to any random number distribution object `d`, thus producing a zero-argument function object such as given by `bind(d,e)`. — *end note*]

**3** Each of the entities specified via this subclause has an associated arithmetic type ([[basic.fundamental]](../../basic/basic.fundamental.md)) identified as `result_type`. With `T` as the `result_type` thus associated with such an entity, that entity is characterized:

- as *boolean* or equivalently as *boolean-valued*, if `T` is `bool`;
- otherwise as *integral* or equivalently as *integer-valued*, if `numeric_limits<T>::is_integer` is `true`;
- otherwise as *floating-point* or equivalently as *real-valued*.

If integer-valued, an entity may optionally be further characterized as *signed* or *unsigned*, according to `numeric_limits<T>::is_signed`.

**4** Unless otherwise specified, all descriptions of calculations in this subclause use mathematical real numbers.

**5** Throughout this subclause, the operators bitand, bitor, and xor denote the respective conventional bitwise operations. Further:

- the operator rshift denotes a bitwise right shift with zero-valued bits appearing in the high bits of the result, and
- the operator lshift<sub>w</sub> denotes a bitwise left shift with zero-valued bits appearing in the low bits of the result, and whose result is always taken modulo 2<sup>*w*</sup>.

## Subclauses

- [26.6.1 Header `<random>` synopsis \[rand.synopsis\]](rand.synopsis.md)
- [26.6.2 Requirements \[rand.req\]](rand.req.md)
  - [26.6.2.1 General requirements \[rand.req.genl\]](rand.req.genl.md)
  - [26.6.2.2 Seed sequence requirements \[rand.req.seedseq\]](rand.req.seedseq.md)
  - [26.6.2.3 Uniform random bit generator requirements \[rand.req.urng\]](rand.req.urng.md)
  - [26.6.2.4 Random number engine requirements \[rand.req.eng\]](rand.req.eng.md)
  - [26.6.2.5 Random number engine adaptor requirements \[rand.req.adapt\]](rand.req.adapt.md)
  - [26.6.2.6 Random number distribution requirements \[rand.req.dist\]](rand.req.dist.md)
- [26.6.3 Random number engine class templates \[rand.eng\]](rand.eng.md)
  - [26.6.3.1 Class template `linear_congruential_engine` \[rand.eng.lcong\]](rand.eng.lcong.md)
  - [26.6.3.2 Class template `mersenne_twister_engine` \[rand.eng.mers\]](rand.eng.mers.md)
  - [26.6.3.3 Class template `subtract_with_carry_engine` \[rand.eng.sub\]](rand.eng.sub.md)
- [26.6.4 Random number engine adaptor class templates \[rand.adapt\]](rand.adapt.md)
  - [26.6.4.1 In general \[rand.adapt.general\]](rand.adapt.general.md)
  - [26.6.4.2 Class template `discard_block_engine` \[rand.adapt.disc\]](rand.adapt.disc.md)
  - [26.6.4.3 Class template `independent_bits_engine` \[rand.adapt.ibits\]](rand.adapt.ibits.md)
  - [26.6.4.4 Class template `shuffle_order_engine` \[rand.adapt.shuf\]](rand.adapt.shuf.md)
- [26.6.5 Engines and engine adaptors with predefined parameters \[rand.predef\]](rand.predef.md)
- [26.6.6 Class `random_device` \[rand.device\]](rand.device.md)
- [26.6.7 Utilities \[rand.util\]](rand.util.md)
  - [26.6.7.1 Class `seed_seq` \[rand.util.seedseq\]](rand.util.seedseq.md)
  - [26.6.7.2 Function template `generate_canonical` \[rand.util.canonical\]](rand.util.canonical.md)
- [26.6.8 Random number distribution class templates \[rand.dist\]](rand.dist.md)
  - [26.6.8.1 In general \[rand.dist.general\]](rand.dist.general.md)
  - [26.6.8.2 Uniform distributions \[rand.dist.uni\]](rand.dist.uni.md)
    - [26.6.8.2.1 Class template `uniform_int_distribution` \[rand.dist.uni.int\]](rand.dist.uni.int.md)
    - [26.6.8.2.2 Class template `uniform_real_distribution` \[rand.dist.uni.real\]](rand.dist.uni.real.md)
  - [26.6.8.3 Bernoulli distributions \[rand.dist.bern\]](rand.dist.bern.md)
    - [26.6.8.3.1 Class `bernoulli_distribution` \[rand.dist.bern.bernoulli\]](rand.dist.bern.bernoulli.md)
    - [26.6.8.3.2 Class template `binomial_distribution` \[rand.dist.bern.bin\]](rand.dist.bern.bin.md)
    - [26.6.8.3.3 Class template `geometric_distribution` \[rand.dist.bern.geo\]](rand.dist.bern.geo.md)
    - [26.6.8.3.4 Class template `negative_binomial_distribution` \[rand.dist.bern.negbin\]](rand.dist.bern.negbin.md)
  - [26.6.8.4 Poisson distributions \[rand.dist.pois\]](rand.dist.pois.md)
    - [26.6.8.4.1 Class template `poisson_distribution` \[rand.dist.pois.poisson\]](rand.dist.pois.poisson.md)
    - [26.6.8.4.2 Class template `exponential_distribution` \[rand.dist.pois.exp\]](rand.dist.pois.exp.md)
    - [26.6.8.4.3 Class template `gamma_distribution` \[rand.dist.pois.gamma\]](rand.dist.pois.gamma.md)
    - [26.6.8.4.4 Class template `weibull_distribution` \[rand.dist.pois.weibull\]](rand.dist.pois.weibull.md)
    - [26.6.8.4.5 Class template `extreme_value_distribution` \[rand.dist.pois.extreme\]](rand.dist.pois.extreme.md)
  - [26.6.8.5 Normal distributions \[rand.dist.norm\]](rand.dist.norm.md)
    - [26.6.8.5.1 Class template `normal_distribution` \[rand.dist.norm.normal\]](rand.dist.norm.normal.md)
    - [26.6.8.5.2 Class template `lognormal_distribution` \[rand.dist.norm.lognormal\]](rand.dist.norm.lognormal.md)
    - [26.6.8.5.3 Class template `chi_squared_distribution` \[rand.dist.norm.chisq\]](rand.dist.norm.chisq.md)
    - [26.6.8.5.4 Class template `cauchy_distribution` \[rand.dist.norm.cauchy\]](rand.dist.norm.cauchy.md)
    - [26.6.8.5.5 Class template `fisher_f_distribution` \[rand.dist.norm.f\]](rand.dist.norm.f.md)
    - [26.6.8.5.6 Class template `student_t_distribution` \[rand.dist.norm.t\]](rand.dist.norm.t.md)
  - [26.6.8.6 Sampling distributions \[rand.dist.samp\]](rand.dist.samp.md)
    - [26.6.8.6.1 Class template `discrete_distribution` \[rand.dist.samp.discrete\]](rand.dist.samp.discrete.md)
    - [26.6.8.6.2 Class template `piecewise_constant_distribution` \[rand.dist.samp.pconst\]](rand.dist.samp.pconst.md)
    - [26.6.8.6.3 Class template `piecewise_linear_distribution` \[rand.dist.samp.plinear\]](rand.dist.samp.plinear.md)
- [26.6.9 Low-quality random number generation \[c.math.rand\]](c.math.rand.md)
