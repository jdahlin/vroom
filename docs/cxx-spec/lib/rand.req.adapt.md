# 26.6.2.5 Random number engine adaptor requirements [rand.req.adapt]

[Contents](../README.md) · up: [26.6.2 Requirements \[rand.req\]](rand.req.md) · prev: [26.6.2.4 Random number engine requirements \[rand.req.eng\]](rand.req.eng.md) · next: [26.6.2.6 Random number distribution requirements \[rand.req.dist\]](rand.req.dist.md)

**1** A *random number engine adaptor* (commonly shortened to *adaptor*) `a` of type `A` is a random number engine that takes values produced by some other random number engine, and applies an algorithm to those values in order to deliver a sequence of values with different randomness properties. An engine `b` of type `B` adapted in this way is termed a *base engine* in this context. The expression `a.base()` shall be valid and shall return a const reference to `a`’s base engine.

**2** The requirements of a random number engine type shall be interpreted as follows with respect to a random number engine adaptor type.

```cpp
A::A();
```

**3** *Effects:* The base engine is initialized as if by its default constructor.

```cpp
bool operator==(const A& a1, const A& a2);
```

**4** *Returns:* `true` if `a1`’s base engine is equal to `a2`’s base engine. Otherwise returns `false`.

```cpp
A::A(result_type s);
```

**5** *Effects:* The base engine is initialized with `s`.

```cpp
template<class Sseq> A::A(Sseq& q);
```

**6** *Effects:* The base engine is initialized with `q`.

```cpp
void seed();
```

**7** *Effects:* With `b` as the base engine, invokes `b.seed()`.

```cpp
void seed(result_type s);
```

**8** *Effects:* With `b` as the base engine, invokes `b.seed(s)`.

```cpp
template<class Sseq> void seed(Sseq& q);
```

**9** *Effects:* With `b` as the base engine, invokes `b.seed(q)`.

**10** `A` shall also meet the following additional requirements:

- The complexity of each function shall not exceed the complexity of the corresponding function applied to the base engine.
- The state of `A` shall include the state of its base engine. The size of `A`’s state shall be no less than the size of the base engine.
- Copying `A`’s state (e.g., during copy construction or copy assignment) shall include copying the state of the base engine of `A`.
- The textual representation of `A` shall include the textual representation of its base engine.
