# 26.6.8.2.2 Class template `uniform_real_distribution` [rand.dist.uni.real]

[Contents](../../README.md) · up: [26.6.8.2 Uniform distributions \[rand.dist.uni\]](rand.dist.uni.md) · prev: [26.6.8.2.1 Class template `uniform_int_distribution` \[rand.dist.uni.int\]](rand.dist.uni.int.md) · next: [26.6.8.3 Bernoulli distributions \[rand.dist.bern\]](rand.dist.bern.md)

**1** A `uniform_real_distribution` random number distribution produces random numbers *x*, *a* ≤ *x* &lt; *b*, distributed according to the constant probability density function

$$p(x\,|\,a,b) = 1 / (b - a) \text{ .}$$

[*Note 1*: This implies that *p*(*x*|*a*,*b*) is undefined when `a == b`. — *end note*]

```cpp
template<class RealType = double>
  class uniform_real_distribution {
  public:
    // types
    using result_type = RealType;
    using param_type  = unspecified;

    // constructors and reset functions
    uniform_real_distribution() : uniform_real_distribution(0.0) {}
    explicit uniform_real_distribution(RealType a, RealType b = 1.0);
    explicit uniform_real_distribution(const param_type& parm);
    void reset();

    // generating functions
    template<class URBG>
      result_type operator()(URBG& g);
    template<class URBG>
      result_type operator()(URBG& g, const param_type& parm);

    // property functions
    result_type a() const;
    result_type b() const;
    param_type param() const;
    void param(const param_type& parm);
    result_type min() const;
    result_type max() const;
  };
```

```cpp
explicit uniform_real_distribution(RealType a, RealType b = 1.0);
```

**2** *Preconditions:* `a` ≤ `b` and `b` - `a` ≤ `numeric_limits<RealType>::max()`.

**3** *Remarks:* `a` and `b` correspond to the respective parameters of the distribution.

```cpp
result_type a() const;
```

**4** *Returns:* The value of the `a` parameter with which the object was constructed.

```cpp
result_type b() const;
```

**5** *Returns:* The value of the `b` parameter with which the object was constructed.
