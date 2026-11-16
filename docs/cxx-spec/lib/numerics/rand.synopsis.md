# 26.6.1 Header `<random>` synopsis [rand.synopsis]

[Contents](../../README.md) · up: [26.6 Random number generation \[rand\]](rand.md) · prev: [26.6 Random number generation \[rand\]](rand.md) · next: [26.6.2 Requirements \[rand.req\]](rand.req.md)

```cpp
#include <initializer_list>

namespace std {
  // [rand.req.urng], uniform random bit generator requirements
  template<class G>
    concept uniform_random_bit_generator = see below;

  // [rand.eng.lcong], class template linear_congruential_engine
  template<class UIntType, UIntType a, UIntType c, UIntType m>
    class linear_congruential_engine;

  // [rand.eng.mers], class template mersenne_twister_engine
  template<class UIntType, size_t w, size_t n, size_t m, size_t r,
           UIntType a, size_t u, UIntType d, size_t s,
           UIntType b, size_t t,
           UIntType c, size_t l, UIntType f>
    class mersenne_twister_engine;

  // [rand.eng.sub], class template subtract_with_carry_engine
  template<class UIntType, size_t w, size_t s, size_t r>
    class subtract_with_carry_engine;

  // [rand.adapt.disc], class template discard_block_engine
  template<class Engine, size_t p, size_t r>
    class discard_block_engine;

  // [rand.adapt.ibits], class template independent_bits_engine
  template<class Engine, size_t w, class UIntType>
    class independent_bits_engine;

  // [rand.adapt.shuf], class template shuffle_order_engine
  template<class Engine, size_t k>
    class shuffle_order_engine;

  // [rand.predef], engines and engine adaptors with predefined parameters
  using minstd_rand0  = see below;
  using minstd_rand   = see below;
  using mt19937       = see below;
  using mt19937_64    = see below;
  using ranlux24_base = see below;
  using ranlux48_base = see below;
  using ranlux24      = see below;
  using ranlux48      = see below;
  using knuth_b       = see below;

  using default_random_engine = see below;

  // [rand.device], class random_device
  class random_device;

  // [rand.util.seedseq], class seed_seq
  class seed_seq;

  // [rand.util.canonical], function template generate_canonical
  template<class RealType, size_t bits, class URBG>
    RealType generate_canonical(URBG& g);

  // [rand.dist.uni.int], class template uniform_int_distribution
  template<class IntType = int>
    class uniform_int_distribution;

  // [rand.dist.uni.real], class template uniform_real_distribution
  template<class RealType = double>
    class uniform_real_distribution;

  // [rand.dist.bern.bernoulli], class bernoulli_distribution
  class bernoulli_distribution;

  // [rand.dist.bern.bin], class template binomial_distribution
  template<class IntType = int>
    class binomial_distribution;

  // [rand.dist.bern.geo], class template geometric_distribution
  template<class IntType = int>
    class geometric_distribution;

  // [rand.dist.bern.negbin], class template negative_binomial_distribution
  template<class IntType = int>
    class negative_binomial_distribution;

  // [rand.dist.pois.poisson], class template poisson_distribution
  template<class IntType = int>
    class poisson_distribution;

  // [rand.dist.pois.exp], class template exponential_distribution
  template<class RealType = double>
    class exponential_distribution;

  // [rand.dist.pois.gamma], class template gamma_distribution
  template<class RealType = double>
    class gamma_distribution;

  // [rand.dist.pois.weibull], class template weibull_distribution
  template<class RealType = double>
    class weibull_distribution;

  // [rand.dist.pois.extreme], class template extreme_value_distribution
  template<class RealType = double>
    class extreme_value_distribution;

  // [rand.dist.norm.normal], class template normal_distribution
  template<class RealType = double>
    class normal_distribution;

  // [rand.dist.norm.lognormal], class template lognormal_distribution
  template<class RealType = double>
    class lognormal_distribution;

  // [rand.dist.norm.chisq], class template chi_squared_distribution
  template<class RealType = double>
    class chi_squared_distribution;

  // [rand.dist.norm.cauchy], class template cauchy_distribution
  template<class RealType = double>
    class cauchy_distribution;

  // [rand.dist.norm.f], class template fisher_f_distribution
  template<class RealType = double>
    class fisher_f_distribution;

  // [rand.dist.norm.t], class template student_t_distribution
  template<class RealType = double>
    class student_t_distribution;

  // [rand.dist.samp.discrete], class template discrete_distribution
  template<class IntType = int>
    class discrete_distribution;

  // [rand.dist.samp.pconst], class template piecewise_constant_distribution
  template<class RealType = double>
    class piecewise_constant_distribution;

  // [rand.dist.samp.plinear], class template piecewise_linear_distribution
  template<class RealType = double>
    class piecewise_linear_distribution;
}
```
