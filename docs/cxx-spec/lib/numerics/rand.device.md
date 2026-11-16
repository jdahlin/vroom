# 26.6.6 Class `random_device` [rand.device]

[Contents](../../README.md) · up: [26.6 Random number generation \[rand\]](rand.md) · prev: [26.6.5 Engines and engine adaptors with predefined parameters \[rand.predef\]](rand.predef.md) · next: [26.6.7 Utilities \[rand.util\]](rand.util.md)

**1** A `random_device` uniform random bit generator produces nondeterministic random numbers.

**2** If implementation limitations prevent generating nondeterministic random numbers, the implementation may employ a random number engine.

```cpp
class random_device {
public:
  // types
  using result_type = unsigned int;

  // generator characteristics
  static constexpr result_type min() { return numeric_limits<result_type>::min(); }
  static constexpr result_type max() { return numeric_limits<result_type>::max(); }

  // constructors
  random_device() : random_device(implementation-defined) {}
  explicit random_device(const string& token);

  // generating functions
  result_type operator()();

  // property functions
  double entropy() const noexcept;

  // no copy functions
  random_device(const random_device&) = delete;
  void operator=(const random_device&) = delete;
};
```

```cpp
explicit random_device(const string& token);
```

**3** *Remarks:* The semantics of the `token` parameter and the token value used by the default constructor are implementation-defined.[^1]

**4** *Throws:* A value of an implementation-defined type derived from `exception` if the `random_device` could not be initialized.

```cpp
double entropy() const noexcept;
```

**5** *Returns:* If the implementation employs a random number engine, returns 0.0. Otherwise, returns an entropy estimate[^2] for the random numbers returned by `operator()`, in the range `min()` to log<sub>2</sub>( `max()`+1).

```cpp
result_type operator()();
```

**6** *Returns:* A nondeterministic random value, uniformly distributed between `min()` and `max()` (inclusive). It is implementation-defined how these values are generated.

**7** *Throws:* A value of an implementation-defined type derived from `exception` if a random number could not be obtained.

[^1]: The parameter is intended to allow an implementation to differentiate between different sources of randomness.
[^2]: If a device has *n* states whose respective probabilities are *P*<sub>0</sub>, …, *P*<sub>*n*-1</sub>, the device entropy *S* is defined as<br> *S* = - ∑<sub>*i*=0</sub><sup>*n*-1</sup> *P*<sub>*i*</sub> · log *P*<sub>*i*</sub>.
