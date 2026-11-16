# 26.6.7.1 Class `seed_seq` [rand.util.seedseq]

[Contents](../../README.md) · up: [26.6.7 Utilities \[rand.util\]](rand.util.md) · prev: [26.6.7 Utilities \[rand.util\]](rand.util.md) · next: [26.6.7.2 Function template `generate_canonical` \[rand.util.canonical\]](rand.util.canonical.md)

```cpp
class seed_seq {
public:
  // types
  using result_type = uint_least32_t;

  // constructors
  seed_seq();
  template<class T>
    seed_seq(initializer_list<T> il);
  template<class InputIterator>
    seed_seq(InputIterator begin, InputIterator end);

  // generating functions
  template<class RandomAccessIterator>
    void generate(RandomAccessIterator begin, RandomAccessIterator end);

  // property functions
  size_t size() const noexcept;
  template<class OutputIterator>
    void param(OutputIterator dest) const;

  // no copy functions
  seed_seq(const seed_seq&) = delete;
  void operator=(const seed_seq&) = delete;

private:
  vector<result_type> v;        // exposition only
};
```

```cpp
seed_seq();
```

**1** *Postconditions:* `v.empty()` is `true`.

**2** *Throws:* Nothing.

```cpp
template<class T>
 seed_seq(initializer_list<T> il);
```

**3** *Mandates:* `T` is an integer type.

**4** *Effects:* Same as `seed_seq(il.begin(), il.end())`.

```cpp
template<class InputIterator>
  seed_seq(InputIterator begin, InputIterator end);
```

**5** *Mandates:* `iterator_traits<InputIterator>::value_type` is an integer type.

**6** *Preconditions:* `InputIterator` meets the *Cpp17InputIterator* requirements ([[input.iterators]](../iterators/input.iterators.md)).

**7** *Effects:* Initializes `v` by the following algorithm:

```cpp
for (InputIterator s = begin; s != end; ++s)
 v.push_back((*s)mod 2^(32));
```

```cpp
template<class RandomAccessIterator>
  void generate(RandomAccessIterator begin, RandomAccessIterator end);
```

**8** *Mandates:* `iterator_traits<RandomAccessIterator>::value_type` is an unsigned integer type capable of accommodating 32-bit quantities.

**9** *Preconditions:* `RandomAccessIterator` meets the *Cpp17RandomAccessIterator* requirements ([[random.access.iterators]](../iterators/random.access.iterators.md)) and the requirements of a mutable iterator.

**10** *Effects:* Does nothing if `begin == end`. Otherwise, with *s* = `v.size()` and *n* = `end` - `begin`, fills the supplied range [`begin`,`end`) according to the following algorithm in which each operation is to be carried out modulo 2<sup>32</sup>, each indexing operator applied to `begin` is to be taken modulo *n*, and *T*(*x*) is defined as *x* xor (*x* rshift 27):

- By way of initialization, set each element of the range to the value `0x8b8b8b8b`. Additionally, for use in subsequent steps, let *p* = (*n* - *t*) / 2 and let *q* = *p* + *t*, where

  $$t = (n \ge 623) \mbox{ ? } 11 \mbox{ : } (n \ge 68) \mbox{ ? } 7 \mbox{ : } (n \ge 39) \mbox{ ? } 5 \mbox{ : } (n \ge 7) \mbox{ ? } 3 \mbox{ : } (n - 1)/2;$$

- With *m* as the larger of *s* + 1 and *n*, transform the elements of the range: iteratively for *k* = 0, …, *m* - 1, calculate values

  $$\begin{aligned}r_1 & = &
         1664525 \cdot T(    \tcode{begin[}k\tcode{]}
                        \xor \tcode{begin[}k+p\tcode{]}
                        \xor \tcode{begin[}k-1 \tcode{]}
                        )
       \\
       r_2 & = & r_1 + \left\{
         \begin{array}{cl}
           s                                  & \mbox{,  } k = 0
           \\
           k \bmod n + \tcode{v[}k-1\tcode{]} & \mbox{,  } 0 < k \le s
           \\
           k \bmod n                          & \mbox{,  } s < k
         \end{array}
       \right.\end{aligned}$$

  and, in order, increment `begin[k+p]` by *r*<sub>1</sub>, increment `begin[k+q]` by *r*<sub>2</sub>, and set `begin[k]` to *r*<sub>2</sub>.

- Transform the elements of the range again, beginning where the previous step ended: iteratively for *k* = *m*, …, *m* + *n* - 1, calculate values

  $$\begin{aligned}r_3 & = &
         1566083941 \cdot T( \tcode{begin[}k  \tcode{]}
                           + \tcode{begin[}k+p\tcode{]}
                           + \tcode{begin[}k-1\tcode{]}
                           )
       \\
       r_4 & = & r_3 - (k \bmod n)\end{aligned}$$

  and, in order, update `begin[k+p]` by xoring it with *r*<sub>3</sub>, update `begin[k+q]` by xoring it with *r*<sub>4</sub>, and set `begin[k]` to *r*<sub>4</sub>.

**11** *Throws:* What and when `RandomAccessIterator` operations of `begin` and `end` throw.

```cpp
size_t size() const noexcept;
```

**12** *Returns:* The number of 32-bit units that would be returned by a call to `param()`.

**13** *Complexity:* Constant time.

```cpp
template<class OutputIterator>
  void param(OutputIterator dest) const;
```

**14** *Mandates:* Values of type `result_type` are writable ([[iterator.requirements.general]](../iterators/iterator.requirements.general.md)) to `dest`.

**15** *Preconditions:* `OutputIterator` meets the *Cpp17OutputIterator* requirements ([[output.iterators]](../iterators/output.iterators.md)).

**16** *Effects:* Copies the sequence of prepared 32-bit units to the given destination, as if by executing the following statement:

```cpp
copy(v.begin(), v.end(), dest);
```

**17** *Throws:* What and when `OutputIterator` operations of `dest` throw.
