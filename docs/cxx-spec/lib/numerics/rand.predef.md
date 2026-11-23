# 26.6.5 Engines and engine adaptors with predefined parameters [rand.predef]

[Contents](../../README.md) · up: [26.6 Random number generation \[rand\]](rand.md) · prev: [26.6.4.4 Class template `shuffle_order_engine` \[rand.adapt.shuf\]](rand.adapt.shuf.md) · next: [26.6.6 Class `random_device` \[rand.device\]](rand.device.md)

```cpp
using minstd_rand0 =
      linear_congruential_engine<uint_fast32_t, 16'807, 0, 2'147'483'647>;
```

**1** *Required behavior:* The 10000<sup></sup>*th* consecutive invocation of a default-constructed object of type `minstd_rand0` produces the value 1043618065.

```cpp
using minstd_rand =
      linear_congruential_engine<uint_fast32_t, 48'271, 0, 2'147'483'647>;
```

**2** *Required behavior:* The 10000<sup></sup>*th* consecutive invocation of a default-constructed object of type `minstd_rand` produces the value 399268537.

```cpp
using mt19937 =
      mersenne_twister_engine<uint_fast32_t, 32, 624, 397, 31,
       0x9908'b0df, 11, 0xffff'ffff, 7, 0x9d2c'5680, 15, 0xefc6'0000, 18, 1'812'433'253>;
```

**3** *Required behavior:* The 10000<sup></sup>*th* consecutive invocation of a default-constructed object of type `mt19937` produces the value 4123659995.

```cpp
using mt19937_64 =
      mersenne_twister_engine<uint_fast64_t, 64, 312, 156, 31,
       0xb502'6f5a'a966'19e9, 29, 0x5555'5555'5555'5555, 17,
       0x71d6'7fff'eda6'0000, 37, 0xfff7'eee0'0000'0000, 43, 6'364'136'223'846'793'005>;
```

**4** *Required behavior:* The 10000<sup></sup>*th* consecutive invocation of a default-constructed object of type `mt19937_64` produces the value 9981545732273789042.

```cpp
using ranlux24_base =
      subtract_with_carry_engine<uint_fast32_t, 24, 10, 24>;
```

**5** *Required behavior:* The 10000<sup></sup>*th* consecutive invocation of a default-constructed object of type `ranlux24_base` produces the value 7937952.

```cpp
using ranlux48_base =
      subtract_with_carry_engine<uint_fast64_t, 48, 5, 12>;
```

**6** *Required behavior:* The 10000<sup></sup>*th* consecutive invocation of a default-constructed object of type `ranlux48_base` produces the value 61839128582725.

```cpp
using ranlux24 = discard_block_engine<ranlux24_base, 223, 23>;
```

**7** *Required behavior:* The 10000<sup></sup>*th* consecutive invocation of a default-constructed object of type `ranlux24` produces the value 9901578.

```cpp
using ranlux48 = discard_block_engine<ranlux48_base, 389, 11>;
```

**8** *Required behavior:* The 10000<sup></sup>*th* consecutive invocation of a default-constructed object of type `ranlux48` produces the value 249142670248501.

```cpp
using knuth_b = shuffle_order_engine<minstd_rand0,256>;
```

**9** *Required behavior:* The 10000<sup></sup>*th* consecutive invocation of a default-constructed object of type `knuth_b` produces the value 1112339016.

```cpp
using default_random_engine = implementation-defined;
```

**10** *Remarks:* The choice of engine type named by this `typedef` is implementation-defined. [*Note 1*: The implementation may select this type on the basis of performance, size, quality, or any combination of such factors, so as to provide at least acceptable engine behavior for relatively casual, inexpert, and/or lightweight use. Because different implementations may select different underlying engine types, code that uses this `typedef` need not generate identical sequences across implementations. — *end note*]
