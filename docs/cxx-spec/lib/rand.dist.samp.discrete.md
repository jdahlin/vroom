# 26.6.8.6.1 Class template `discrete_distribution` [rand.dist.samp.discrete]

[Contents](../README.md) · up: [26.6.8.6 Sampling distributions \[rand.dist.samp\]](rand.dist.samp.md) · prev: [26.6.8.6 Sampling distributions \[rand.dist.samp\]](rand.dist.samp.md) · next: [26.6.8.6.2 Class template `piecewise_constant_distribution` \[rand.dist.samp.pconst\]](rand.dist.samp.pconst.md)

**1** A `discrete_distribution` random number distribution produces random integers *i*, 0 ≤ *i* &lt; *n*, distributed according to the discrete probability function

$$P(i \,|\, p_0, \dotsc, p_{n-1}) = p_i \text{ .}$$

**2** Unless specified otherwise, the distribution parameters are calculated as: *p*<sub>*k*</sub> = *w*<sub>*k*</sub> / *S* for *k* = 0, …, *n* - 1, in which the values *w*<sub>*k*</sub>, commonly known as the *weights*, shall be non-negative, non-NaN, and non-infinity. Moreover, the following relation shall hold: 0 &lt; *S* = *w*<sub>0</sub> + …+ *w*<sub>*n* - 1</sub>.

```cpp
template<class IntType = int>
  class discrete_distribution {
  public:
    // types
    using result_type = IntType;
    using param_type  = unspecified;

    // constructor and reset functions
    discrete_distribution();
    template<class InputIterator>
      discrete_distribution(InputIterator firstW, InputIterator lastW);
    discrete_distribution(initializer_list<double> wl);
    template<class UnaryOperation>
      discrete_distribution(size_t nw, double xmin, double xmax, UnaryOperation fw);
    explicit discrete_distribution(const param_type& parm);
    void reset();

    // generating functions
    template<class URBG>
      result_type operator()(URBG& g);
    template<class URBG>
      result_type operator()(URBG& g, const param_type& parm);

    // property functions
    vector<double> probabilities() const;
    param_type param() const;
    void param(const param_type& parm);
    result_type min() const;
    result_type max() const;
  };
```

```cpp
discrete_distribution();
```

**3** *Effects:* Constructs a `discrete_distribution` object with *n* = 1 and *p*<sub>0</sub> = 1. [*Note 1*: Such an object will always deliver the value 0. — *end note*]

```cpp
template<class InputIterator>
  discrete_distribution(InputIterator firstW, InputIterator lastW);
```

**4** *Mandates:* `is_convertible_v<iterator_traits<InputIterator>::value_type, double>` is `true`.

**5** *Preconditions:* `InputIterator` meets the *Cpp17InputIterator* requirements ([[input.iterators]](input.iterators.md)). If `firstW == lastW`, let *n* = 1 and *w*<sub>0</sub> = 1. Otherwise, [`firstW`, `lastW`) forms a sequence *w* of length *n* > 0.

**6** *Effects:* Constructs a `discrete_distribution` object with probabilities given by the formula above.

```cpp
discrete_distribution(initializer_list<double> wl);
```

**7** *Effects:* Same as `discrete_distribution(wl.begin(), wl.end())`.

```cpp
template<class UnaryOperation>
  discrete_distribution(size_t nw, double xmin, double xmax, UnaryOperation fw);
```

**8** *Mandates:* `is_invocable_r_v<double, UnaryOperation&, double>` is `true`.

**9** *Preconditions:* If `nw` = 0, let *n* = 1, otherwise let *n* = `nw`. The relation 0 &lt; δ = (`xmax` - `xmin`) / *n* holds.

**10** *Effects:* Constructs a `discrete_distribution` object with probabilities given by the formula above, using the following values: If `nw` = 0, let *w*<sub>0</sub> = 1. Otherwise, let *w*<sub>*k*</sub> = `fw`(`xmin` + *k* · δ + δ / 2) for *k* = 0, …, *n* - 1.

**11** *Complexity:* The number of invocations of `fw` does not exceed *n*.

```cpp
vector<double> probabilities() const;
```

**12** *Returns:* A `vector<double>` whose `size` member returns *n* and whose `operator[]` member returns *p*<sub>*k*</sub> when invoked with argument *k* for *k* = 0, …, *n* - 1.
