# 26.6.8.6.2 Class template `piecewise_constant_distribution` [rand.dist.samp.pconst]

[Contents](../README.md) · up: [26.6.8.6 Sampling distributions \[rand.dist.samp\]](rand.dist.samp.md) · prev: [26.6.8.6.1 Class template `discrete_distribution` \[rand.dist.samp.discrete\]](rand.dist.samp.discrete.md) · next: [26.6.8.6.3 Class template `piecewise_linear_distribution` \[rand.dist.samp.plinear\]](rand.dist.samp.plinear.md)

**1** A `piecewise_constant_distribution` random number distribution produces random numbers *x*, *b*<sub>0</sub> ≤ *x* &lt; *b*<sub>*n*</sub>, uniformly distributed over each subinterval [ *b*<sub>*i*</sub>, *b*<sub>*i*+1</sub> ) according to the probability density function

$$p(x \,|\, b_0, \dotsc, b_n, \; \rho_0, \dotsc, \rho_{n-1}) = \rho_i
   \text{ , for $b_i \le x < b_{i+1}$.}$$

**2** The *n* + 1 distribution parameters *b*<sub>*i*</sub>, also known as this distribution’s *interval boundaries*, shall satisfy the relation *b*<sub>*i*</sub> &lt; *b*<sub>*i* + 1</sub> for *i* = 0, …, *n* - 1. Unless specified otherwise, the remaining *n* distribution parameters are calculated as:

$$\rho_k = \frac{w_k}{S \cdot (b_{k+1}-b_k)} \text{ for } k = 0, \dotsc, n - 1 \text{ ,}$$

in which the values *w*<sub>*k*</sub>, commonly known as the *weights*, shall be non-negative, non-NaN, and non-infinity. Moreover, the following relation shall hold: 0 &lt; *S* = *w*<sub>0</sub> + …+ *w*<sub>*n*-1</sub>.

```cpp
template<class RealType = double>
  class piecewise_constant_distribution {
  public:
    // types
    using result_type = RealType;
    using param_type  = unspecified;

    // constructor and reset functions
    piecewise_constant_distribution();
    template<class InputIteratorB, class InputIteratorW>
      piecewise_constant_distribution(InputIteratorB firstB, InputIteratorB lastB,
                                      InputIteratorW firstW);
    template<class UnaryOperation>
      piecewise_constant_distribution(initializer_list<RealType> bl, UnaryOperation fw);
    template<class UnaryOperation>
      piecewise_constant_distribution(size_t nw, RealType xmin, RealType xmax,
                                      UnaryOperation fw);
    explicit piecewise_constant_distribution(const param_type& parm);
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
piecewise_constant_distribution();
```

**3** *Effects:* Constructs a `piecewise_constant_distribution` object with *n* = 1, ρ<sub>0</sub> = 1, *b*<sub>0</sub> = 0, and *b*<sub>1</sub> = 1.

```cpp
template<class InputIteratorB, class InputIteratorW>
 piecewise_constant_distribution(InputIteratorB firstB, InputIteratorB lastB,
                                 InputIteratorW firstW);
```

**4** *Mandates:* Both of

- `is_convertible_v<iterator_traits<InputIteratorB>::value_type, double>`
- `is_convertible_v<iterator_traits<InputIteratorW>::value_type, double>`

are `true`.

**5** *Preconditions:* `InputIteratorB` and `InputIteratorW` each meet the *Cpp17InputIterator* requirements ([[input.iterators]](input.iterators.md)). If `firstB == lastB` or `++firstB == lastB`, let *n* = 1, *w*<sub>0</sub> = 1, *b*<sub>0</sub> = 0, and *b*<sub>1</sub> = 1. Otherwise, [`firstB`, `lastB`) forms a sequence *b* of length *n*+1, the length of the sequence *w* starting from `firstW` is at least *n*, and any *w*<sub>*k*</sub> for *k* ≥ *n* are ignored by the distribution.

**6** *Effects:* Constructs a `piecewise_constant_distribution` object with parameters as specified above.

```cpp
template<class UnaryOperation>
 piecewise_constant_distribution(initializer_list<RealType> bl, UnaryOperation fw);
```

**7** *Mandates:* `is_invocable_r_v<double, UnaryOperation&, double>` is `true`.

**8** *Effects:* Constructs a `piecewise_constant_distribution` object with parameters taken or calculated from the following values: If `bl.size()` &lt; 2, let *n* = 1, *w*<sub>0</sub> = 1, *b*<sub>0</sub> = 0, and *b*<sub>1</sub> = 1. Otherwise, let [`bl.begin()`, `bl.end()`) form a sequence *b*<sub>0</sub>, …, *b*<sub>*n*</sub>, and let *w*<sub>*k*</sub> = `fw`((*b*<sub>*k*+1</sub> + *b*<sub>*k*</sub>) / 2) for *k* = 0, …, *n* - 1.

**9** *Complexity:* The number of invocations of `fw` does not exceed *n*.

```cpp
template<class UnaryOperation>
 piecewise_constant_distribution(size_t nw, RealType xmin, RealType xmax, UnaryOperation fw);
```

**10** *Mandates:* `is_invocable_r_v<double, UnaryOperation&, double>` is `true`.

**11** *Preconditions:* If `nw` = 0, let *n* = 1, otherwise let *n* = `nw`. The relation 0 &lt; δ = (`xmax` - `xmin`) / *n* holds.

**12** *Effects:* Constructs a `piecewise_constant_distribution` object with parameters taken or calculated from the following values: Let *b*<sub>*k*</sub> = `xmin` + *k* · δ for *k* = 0, …, *n*, and *w*<sub>*k*</sub> = `fw`(*b*<sub>*k*</sub> + δ / 2) for *k* = 0, …, *n* - 1.

**13** *Complexity:* The number of invocations of `fw` does not exceed *n*.

```cpp
vector<result_type> intervals() const;
```

**14** *Returns:* A `vector<result_type>` whose `size` member returns *n* + 1 and whose `operator[]` member returns *b*<sub>*k*</sub> when invoked with argument *k* for *k* = 0, …, *n*.

```cpp
vector<result_type> densities() const;
```

**15** *Returns:* A `vector<result_type>` whose `size` member returns *n* and whose `operator[]` member returns ρ<sub>*k*</sub> when invoked with argument *k* for *k* = 0, …, *n* - 1.
