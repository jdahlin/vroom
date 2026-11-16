# 26.6.8.3.2 Class template `binomial_distribution` [rand.dist.bern.bin]

[Contents](../README.md) · up: [26.6.8.3 Bernoulli distributions \[rand.dist.bern\]](rand.dist.bern.md) · prev: [26.6.8.3.1 Class `bernoulli_distribution` \[rand.dist.bern.bernoulli\]](rand.dist.bern.bernoulli.md) · next: [26.6.8.3.3 Class template `geometric_distribution` \[rand.dist.bern.geo\]](rand.dist.bern.geo.md)

**1** A `binomial_distribution` random number distribution produces integer values *i* ≥ 0 distributed according to the discrete probability function

$$P(i\,|\,t,p) = \binom{t}{i} \cdot p^i \cdot (1-p)^{t-i} \text{ .}$$

```cpp
template<class IntType = int>
  class binomial_distribution {
  public:
    // types
    using result_type = IntType;
    using param_type  = unspecified;

    // constructors and reset functions
    binomial_distribution() : binomial_distribution(1) {}
    explicit binomial_distribution(IntType t, double p = 0.5);
    explicit binomial_distribution(const param_type& parm);
    void reset();

    // generating functions
    template<class URBG>
      result_type operator()(URBG& g);
    template<class URBG>
      result_type operator()(URBG& g, const param_type& parm);

    // property functions
    IntType t() const;
    double p() const;
    param_type param() const;
    void param(const param_type& parm);
    result_type min() const;
    result_type max() const;
  };
```

```cpp
explicit binomial_distribution(IntType t, double p = 0.5);
```

**2** *Preconditions:* 0 ≤ `p` ≤ 1 and 0 ≤ `t`.

**3** *Remarks:* `t` and `p` correspond to the respective parameters of the distribution.

```cpp
IntType t() const;
```

**4** *Returns:* The value of the `t` parameter with which the object was constructed.

```cpp
double p() const;
```

**5** *Returns:* The value of the `p` parameter with which the object was constructed.
