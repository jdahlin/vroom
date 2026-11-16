# 26.6.8.5.2 Class template `lognormal_distribution` [rand.dist.norm.lognormal]

[Contents](../README.md) · up: [26.6.8.5 Normal distributions \[rand.dist.norm\]](rand.dist.norm.md) · prev: [26.6.8.5.1 Class template `normal_distribution` \[rand.dist.norm.normal\]](rand.dist.norm.normal.md) · next: [26.6.8.5.3 Class template `chi_squared_distribution` \[rand.dist.norm.chisq\]](rand.dist.norm.chisq.md)

**1** A `lognormal_distribution` random number distribution produces random numbers *x* > 0 distributed according to the probability density function

$$p(x\,|\,m,s) = \frac{1}{s x \sqrt{2 \pi}}
     \cdot \exp{\left(-\frac{(\ln{x} - m)^2}{2 s^2}\right)}
     \text{ .}$$

```cpp
template<class RealType = double>
  class lognormal_distribution {
  public:
    // types
    using result_type = RealType;
    using param_type  = unspecified;

    // constructor and reset functions
    lognormal_distribution() : lognormal_distribution(0.0) {}
    explicit lognormal_distribution(RealType m, RealType s = 1.0);
    explicit lognormal_distribution(const param_type& parm);
    void reset();

    // generating functions
    template<class URBG>
      result_type operator()(URBG& g);
    template<class URBG>
      result_type operator()(URBG& g, const param_type& parm);

    // property functions
    RealType m() const;
    RealType s() const;
    param_type param() const;
    void param(const param_type& parm);
    result_type min() const;
    result_type max() const;
  };
```

```cpp
explicit lognormal_distribution(RealType m, RealType s = 1.0);
```

**2** *Preconditions:* 0 &lt; `s`.

**3** *Remarks:* `m` and `s` correspond to the respective parameters of the distribution.

```cpp
RealType m() const;
```

**4** *Returns:* The value of the `m` parameter with which the object was constructed.

```cpp
RealType s() const;
```

**5** *Returns:* The value of the `s` parameter with which the object was constructed.
