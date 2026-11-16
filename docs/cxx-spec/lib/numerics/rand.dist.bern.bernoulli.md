# 26.6.8.3.1 Class `bernoulli_distribution` [rand.dist.bern.bernoulli]

[Contents](../../README.md) · up: [26.6.8.3 Bernoulli distributions \[rand.dist.bern\]](rand.dist.bern.md) · prev: [26.6.8.3 Bernoulli distributions \[rand.dist.bern\]](rand.dist.bern.md) · next: [26.6.8.3.2 Class template `binomial_distribution` \[rand.dist.bern.bin\]](rand.dist.bern.bin.md)

**1** A `bernoulli_distribution` random number distribution produces `bool` values *b* distributed according to the discrete probability function

$$P(b\,|\,p) = \left\{ \begin{array}{ll}
                          p     & \text{ if $b = \tcode{true}$, or} \\
                          1 - p & \text{ if $b = \tcode{false}$.}
                          \end{array}\right.$$

```cpp
class bernoulli_distribution {
public:
  // types
  using result_type = bool;
  using param_type  = unspecified;

  // constructors and reset functions
  bernoulli_distribution() : bernoulli_distribution(0.5) {}
  explicit bernoulli_distribution(double p);
  explicit bernoulli_distribution(const param_type& parm);
  void reset();

  // generating functions
  template<class URBG>
    result_type operator()(URBG& g);
  template<class URBG>
    result_type operator()(URBG& g, const param_type& parm);

  // property functions
  double p() const;
  param_type param() const;
  void param(const param_type& parm);
  result_type min() const;
  result_type max() const;
};
```

```cpp
explicit bernoulli_distribution(double p);
```

**2** *Preconditions:* 0 ≤ `p` ≤ 1.

**3** *Remarks:* `p` corresponds to the parameter of the distribution.

```cpp
double p() const;
```

**4** *Returns:* The value of the `p` parameter with which the object was constructed.
