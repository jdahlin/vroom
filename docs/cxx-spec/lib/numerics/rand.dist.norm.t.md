# 26.6.8.5.6 Class template `student_t_distribution` [rand.dist.norm.t]

[Contents](../../README.md) · up: [26.6.8.5 Normal distributions \[rand.dist.norm\]](rand.dist.norm.md) · prev: [26.6.8.5.5 Class template `fisher_f_distribution` \[rand.dist.norm.f\]](rand.dist.norm.f.md) · next: [26.6.8.6 Sampling distributions \[rand.dist.samp\]](rand.dist.samp.md)

**1** A `student_t_distribution` random number distribution produces random numbers *x* distributed according to the probability density function

$$p(x\,|\,n) = \frac{1}{\sqrt{n \pi}}
     \cdot \frac{\Gamma\big((n+1)/2\big)}{\Gamma(n/2)}
     \cdot \left(1 + \frac{x^2}{n} \right)^{-(n+1)/2}
     \text{ .}$$

```cpp
template<class RealType = double>
  class student_t_distribution {
  public:
    // types
    using result_type = RealType;
    using param_type  = unspecified;

    // constructor and reset functions
    student_t_distribution() : student_t_distribution(1.0) {}
    explicit student_t_distribution(RealType n);
    explicit student_t_distribution(const param_type& parm);
    void reset();

    // generating functions
    template<class URBG>
      result_type operator()(URBG& g);
    template<class URBG>
      result_type operator()(URBG& g, const param_type& parm);

    // property functions
    RealType n() const;
    param_type param() const;
    void param(const param_type& parm);
    result_type min() const;
    result_type max() const;
  };
```

```cpp
explicit student_t_distribution(RealType n);
```

**2** *Preconditions:* 0 &lt; `n`.

**3** *Remarks:* `n` corresponds to the parameter of the distribution.

```cpp
RealType n() const;
```

**4** *Returns:* The value of the `n` parameter with which the object was constructed.
