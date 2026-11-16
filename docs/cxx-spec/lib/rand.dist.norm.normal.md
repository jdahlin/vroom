# 26.6.8.5.1 Class template `normal_distribution` [rand.dist.norm.normal]

[Contents](../README.md) · up: [26.6.8.5 Normal distributions \[rand.dist.norm\]](rand.dist.norm.md) · prev: [26.6.8.5 Normal distributions \[rand.dist.norm\]](rand.dist.norm.md) · next: [26.6.8.5.2 Class template `lognormal_distribution` \[rand.dist.norm.lognormal\]](rand.dist.norm.lognormal.md)

**1** A `normal_distribution` random number distribution produces random numbers *x* distributed according to the probability density function

$$p(x\,|\,\mu,\sigma)
      = \frac{1}{\sigma \sqrt{2\pi}}
        \cdot
        \exp{\left(- \, \frac{(x - \mu)^2}
                             {2 \sigma^2}
             \right)
            }
 \text{ .}$$

The distribution parameters μ and σ are also known as this distribution’s *mean* and *standard deviation*.

```cpp
template<class RealType = double>
  class normal_distribution {
  public:
    // types
    using result_type = RealType;
    using param_type  = unspecified;

    // constructors and reset functions
    normal_distribution() : normal_distribution(0.0) {}
    explicit normal_distribution(RealType mean, RealType stddev = 1.0);
    explicit normal_distribution(const param_type& parm);
    void reset();

    // generating functions
    template<class URBG>
      result_type operator()(URBG& g);
    template<class URBG>
      result_type operator()(URBG& g, const param_type& parm);

    // property functions
    RealType mean() const;
    RealType stddev() const;
    param_type param() const;
    void param(const param_type& parm);
    result_type min() const;
    result_type max() const;
  };
```

```cpp
explicit normal_distribution(RealType mean, RealType stddev = 1.0);
```

**2** *Preconditions:* 0 &lt; `stddev`.

**3** *Remarks:* `mean` and `stddev` correspond to the respective parameters of the distribution.

```cpp
RealType mean() const;
```

**4** *Returns:* The value of the `mean` parameter with which the object was constructed.

```cpp
RealType stddev() const;
```

**5** *Returns:* The value of the `stddev` parameter with which the object was constructed.
