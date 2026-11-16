# 26.6.8.5.5 Class template `fisher_f_distribution` [rand.dist.norm.f]

[Contents](../README.md) · up: [26.6.8.5 Normal distributions \[rand.dist.norm\]](rand.dist.norm.md) · prev: [26.6.8.5.4 Class template `cauchy_distribution` \[rand.dist.norm.cauchy\]](rand.dist.norm.cauchy.md) · next: [26.6.8.5.6 Class template `student_t_distribution` \[rand.dist.norm.t\]](rand.dist.norm.t.md)

**1** A `fisher_f_distribution` random number distribution produces random numbers *x* ≥ 0 distributed according to the probability density function

$$p(x\,|\,m,n) = \frac{\Gamma\big((m+n)/2\big)}{\Gamma(m/2) \; \Gamma(n/2)}
     \cdot \left(\frac{m}{n}\right)^{m/2}
     \cdot x^{(m/2)-1}
     \cdot \left(1 + \frac{m x}{n}\right)^{-(m + n)/2}
     \text{ .}$$

```cpp
template<class RealType = double>
  class fisher_f_distribution {
  public:
    // types
    using result_type = RealType;
    using param_type  = unspecified;

    // constructor and reset functions
    fisher_f_distribution() : fisher_f_distribution(1.0) {}
    explicit fisher_f_distribution(RealType m, RealType n = 1.0);
    explicit fisher_f_distribution(const param_type& parm);
    void reset();

    // generating functions
    template<class URBG>
      result_type operator()(URBG& g);
    template<class URBG>
      result_type operator()(URBG& g, const param_type& parm);

    // property functions
    RealType m() const;
    RealType n() const;
    param_type param() const;
    void param(const param_type& parm);
    result_type min() const;
    result_type max() const;
  };
```

```cpp
explicit fisher_f_distribution(RealType m, RealType n = 1);
```

**2** *Preconditions:* 0 &lt; `m` and 0 &lt; `n`.

**3** *Remarks:* `m` and `n` correspond to the respective parameters of the distribution.

```cpp
RealType m() const;
```

**4** *Returns:* The value of the `m` parameter with which the object was constructed.

```cpp
RealType n() const;
```

**5** *Returns:* The value of the `n` parameter with which the object was constructed.
