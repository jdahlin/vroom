# 26.6.8.6.3 Class template `piecewise_linear_distribution` [rand.dist.samp.plinear]

[Contents](../README.md) · up: [26.6.8.6 Sampling distributions \[rand.dist.samp\]](rand.dist.samp.md) · prev: [26.6.8.6.2 Class template `piecewise_constant_distribution` \[rand.dist.samp.pconst\]](rand.dist.samp.pconst.md) · next: [26.6.9 Low-quality random number generation \[c.math.rand\]](c.math.rand.md)

**1** A `piecewise_linear_distribution` random number distribution produces random numbers *x*, *b*<sub>0</sub> ≤ *x* &lt; *b*<sub>*n*</sub>, distributed over each subinterval [*b*<sub>*i*</sub>, *b*<sub>*i*+1</sub>) according to the probability density function

$$p(x \,|\, b_0, \dotsc, b_n, \; \rho_0, \dotsc, \rho_n)
     = \rho_{i}   \cdot {\frac{b_{i+1} - x}{b_{i+1} - b_i}}
     + \rho_{i+1} \cdot {\frac{x - b_i}{b_{i+1} - b_i}}
     \text{ , for $b_i \le x < b_{i+1}$.}$$

**2** The *n* + 1 distribution parameters *b*<sub>*i*</sub>, also known as this distribution’s *interval boundaries*, shall satisfy the relation *b*<sub>*i*</sub> &lt; *b*<sub>*i*+1</sub> for *i* = 0, …, *n* - 1. Unless specified otherwise, the remaining *n* + 1 distribution parameters are calculated as ρ<sub>*k*</sub> = *w*<sub>*k*</sub> / *S* for *k* = 0, …, *n*, in which the values *w*<sub>*k*</sub>, commonly known as the *weights at boundaries*, shall be non-negative, non-NaN, and non-infinity. Moreover, the following relation shall hold:

$$0 < S = \frac{1}{2} \cdot \sum_{k=0}^{n-1} (w_k + w_{k+1}) \cdot (b_{k+1} - b_k) \text{ .}$$

```cpp
template<class RealType = double>
  class piecewise_linear_distribution {
  public:
    // types
    using result_type = RealType;
    using param_type  = unspecified;

    // constructor and reset functions
    piecewise_linear_distribution();
    template<class InputIteratorB, class InputIteratorW>
      piecewise_linear_distribution(InputIteratorB firstB, InputIteratorB lastB,
                                    InputIteratorW firstW);
    template<class UnaryOperation>
      piecewise_linear_distribution(initializer_list<RealType> bl, UnaryOperation fw);
    template<class UnaryOperation>
      piecewise_linear_distribution(size_t nw, RealType xmin, RealType xmax, UnaryOperation fw);
    explicit piecewise_linear_distribution(const param_type& parm);
    void reset();

    // generating functions
    template<class URBG>
      result_type operator()(URBG& g);
    template<class URBG>
      result_type operator()(URBG& g, const param_type& parm);

    // property functions
    vector<result_type> intervals() const;
    vector<result_type> densities() const;
    param_type param() const;
    void param(const param_type& parm);
    result_type min() const;
    result_type max() const;
  };
```

```cpp
piecewise_linear_distribution();
```

**3** *Effects:* Constructs a `piecewise_linear_distribution` object with *n* = 1, ρ<sub>0</sub> = ρ<sub>1</sub> = 1, *b*<sub>0</sub> = 0, and *b*<sub>1</sub> = 1.

```cpp
template<class InputIteratorB, class InputIteratorW>
 piecewise_linear_distribution(InputIteratorB firstB, InputIteratorB lastB,
                               InputIteratorW firstW);
```

**4** *Mandates:* `is_invocable_r_v<double, UnaryOperation&, double>` is `true`.

**5** *Preconditions:* `InputIteratorB` and `InputIteratorW` each meet the *Cpp17InputIterator* requirements ([[input.iterators]](input.iterators.md)). If `firstB == lastB` or `++firstB == lastB`, let *n* = 1, ρ<sub>0</sub> = ρ<sub>1</sub> = 1, *b*<sub>0</sub> = 0, and *b*<sub>1</sub> = 1. Otherwise, [`firstB`, `lastB`) forms a sequence *b* of length *n*+1, the length of the sequence *w* starting from `firstW` is at least *n*+1, and any *w*<sub>*k*</sub> for *k* ≥ *n* + 1 are ignored by the distribution.

**6** *Effects:* Constructs a `piecewise_linear_distribution` object with parameters as specified above.

```cpp
template<class UnaryOperation>
 piecewise_linear_distribution(initializer_list<RealType> bl, UnaryOperation fw);
```

**7** *Mandates:* `is_invocable_r_v<double, UnaryOperation&, double>` is `true`.

**8** *Effects:* Constructs a `piecewise_linear_distribution` object with parameters taken or calculated from the following values: If `bl.size()` &lt; 2, let *n* = 1, ρ<sub>0</sub> = ρ<sub>1</sub> = 1, *b*<sub>0</sub> = 0, and *b*<sub>1</sub> = 1. Otherwise, let [`bl.begin(),` `bl.end()`) form a sequence *b*<sub>0</sub>, …, *b*<sub>*n*</sub>, and let *w*<sub>*k*</sub> = `fw`(*b*<sub>*k*</sub>) for *k* = 0, …, *n*.

**9** *Complexity:* The number of invocations of `fw` does not exceed *n*+1.

```cpp
template<class UnaryOperation>
 piecewise_linear_distribution(size_t nw, RealType xmin, RealType xmax, UnaryOperation fw);
```

**10** *Mandates:* `is_invocable_r_v<double, UnaryOperation&, double>` is `true`.

**11** *Preconditions:* If `nw` = 0, let *n* = 1, otherwise let *n* = `nw`. The relation 0 &lt; δ = (`xmax` - `xmin`) / *n* holds.

**12** *Effects:* Constructs a `piecewise_linear_distribution` object with parameters taken or calculated from the following values: Let *b*<sub>*k*</sub> = `xmin` + *k* · δ for *k* = 0, …, *n*, and *w*<sub>*k*</sub> = `fw`(*b*<sub>*k*</sub>) for *k* = 0, …, *n*.

**13** *Complexity:* The number of invocations of `fw` does not exceed *n*+1.

```cpp
vector<result_type> intervals() const;
```

**14** *Returns:* A `vector<result_type>` whose `size` member returns *n* + 1 and whose `operator[]` member returns *b*<sub>*k*</sub> when invoked with argument *k* for *k* = 0, …, *n*.

```cpp
vector<result_type> densities() const;
```

**15** *Returns:* A `vector<result_type>` whose `size` member returns *n* and whose `operator[]` member returns ρ<sub>*k*</sub> when invoked with argument *k* for *k* = 0, …, *n*.
