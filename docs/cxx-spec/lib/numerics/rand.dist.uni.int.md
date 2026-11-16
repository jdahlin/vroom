# 26.6.8.2.1 Class template `uniform_int_distribution` [rand.dist.uni.int]

[Contents](../../README.md) · up: [26.6.8.2 Uniform distributions \[rand.dist.uni\]](rand.dist.uni.md) · prev: [26.6.8.2 Uniform distributions \[rand.dist.uni\]](rand.dist.uni.md) · next: [26.6.8.2.2 Class template `uniform_real_distribution` \[rand.dist.uni.real\]](rand.dist.uni.real.md)

**1** A `uniform_int_distribution` random number distribution produces random integers *i*, *a* ≤ *i* ≤ *b*, distributed according to the constant discrete probability function

$$P(i\,|\,a,b) = 1 / (b - a + 1) \text{ .}$$

```cpp
template<class IntType = int>
  class uniform_int_distribution {
  public:
    // types
    using result_type = IntType;
    using param_type  = unspecified;

    // constructors and reset functions
    uniform_int_distribution() : uniform_int_distribution(0) {}
    explicit uniform_int_distribution(IntType a, IntType b = numeric_limits<IntType>::max());
    explicit uniform_int_distribution(const param_type& parm);
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
explicit uniform_int_distribution(IntType a, IntType b = numeric_limits<IntType>::max());
```

**2** *Preconditions:* `a` ≤ `b`.

**3** *Remarks:* `a` and `b` correspond to the respective parameters of the distribution.

```cpp
result_type a() const;
```

**4** *Returns:* The value of the `a` parameter with which the object was constructed.

```cpp
result_type b() const;
```

**5** *Returns:* The value of the `b` parameter with which the object was constructed.
