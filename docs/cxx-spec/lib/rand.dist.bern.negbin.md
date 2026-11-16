# 26.6.8.3.4 Class template `negative_binomial_distribution` [rand.dist.bern.negbin]

[Contents](../README.md) · up: [26.6.8.3 Bernoulli distributions \[rand.dist.bern\]](rand.dist.bern.md) · prev: [26.6.8.3.3 Class template `geometric_distribution` \[rand.dist.bern.geo\]](rand.dist.bern.geo.md) · next: [26.6.8.4 Poisson distributions \[rand.dist.pois\]](rand.dist.pois.md)

**1** A `negative_binomial_distribution` random number distribution produces random integers *i* ≥ 0 distributed according to the discrete probability function

$$P(i\,|\,k,p) = \binom{k+i-1}{i} \cdot p^k \cdot (1-p)^i \text{ .}$$

[*Note 1*: This implies that *P*(*i*|*k*,*p*) is undefined when `p == 1`. — *end note*]

```cpp
template<class IntType = int>
  class negative_binomial_distribution {
  public:
    // types
    using result_type = IntType;
    using param_type  = unspecified;

    // constructor and reset functions
    negative_binomial_distribution() : negative_binomial_distribution(1) {}
    explicit negative_binomial_distribution(IntType k, double p = 0.5);
    explicit negative_binomial_distribution(const param_type& parm);
    void reset();

    // generating functions
    template<class URBG>
      result_type operator()(URBG& g);
    template<class URBG>
      result_type operator()(URBG& g, const param_type& parm);

    // property functions
    IntType k() const;
    double p() const;
    param_type param() const;
    void param(const param_type& parm);
    result_type min() const;
    result_type max() const;
  };
```

```cpp
explicit negative_binomial_distribution(IntType k, double p = 0.5);
```

**2** *Preconditions:* 0 &lt; `p` ≤ 1 and 0 &lt; `k`.

**3** *Remarks:* `k` and `p` correspond to the respective parameters of the distribution.

```cpp
IntType k() const;
```

**4** *Returns:* The value of the `k` parameter with which the object was constructed.

```cpp
double p() const;
```

**5** *Returns:* The value of the `p` parameter with which the object was constructed.
