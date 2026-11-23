# 26.6.2.2 Seed sequence requirements [rand.req.seedseq]

[Contents](../../README.md) · up: [26.6.2 Requirements \[rand.req\]](rand.req.md) · prev: [26.6.2.1 General requirements \[rand.req.genl\]](rand.req.genl.md) · next: [26.6.2.3 Uniform random bit generator requirements \[rand.req.urng\]](rand.req.urng.md)

**1** A *seed sequence* is an object that consumes a sequence of integer-valued data and produces a requested number of unsigned integer values *i*, 0 ≤ *i* &lt; 2<sup>32</sup>, based on the consumed data. [*Note 1*: Such an object provides a mechanism to avoid replication of streams of random variates. This can be useful, for example, in applications requiring large numbers of random number engines. — *end note*]

**2** A class `S` meets the requirements of a seed sequence if the expressions shown in [Table 93](rand.req.seedseq.md#tab:rand.req.seedseq) are valid and have the indicated semantics, and if `S` also meets all other requirements of this subclause [[rand.req.seedseq]](rand.req.seedseq.md). In that Table and throughout this subclause:

- `T` is the type named by `S`’s associated `result_type`;
- `q` is a value of `S` and `r` is a possibly const value of `S`;
- `ib` and `ie` are input iterators with an unsigned integer `value_type` of at least 32 bits;
- `rb` and `re` are mutable random access iterators with an unsigned integer `value_type` of at least 32 bits;
- `ob` is an output iterator; and
- `il` is a value of `initializer_list<T>`.

<a id="tab:rand.req.seedseq"></a>
**Table 93: Seed sequence requirements** [tab:rand.req.seedseq]

| Expression | Return type | Pre/post-condition | Complexity |
| --- | --- | --- | --- |
| `S::result_type` | `T` | `T` is an unsigned integer type ([[basic.fundamental]](../../basic/basic.fundamental.md)) of at least 32 bits. | compile-time |
| `S()` |  | Creates a seed sequence with the same initial state as all other default-constructed seed sequences of type `S`. | constant |
| `S(ib,ie)` |  | Creates a seed sequence having internal state that depends on some or all of the bits of the supplied sequence [`ib`,`ie`). | 𝑂(`ie` - `ib`) |
| `S(il)` |  | Same as `S(il.begin(), il.end())`. | same as `S(il.begin(), il.end())` |
| `q.generate(rb,re)` | `void` | Does nothing if `rb == re`. Otherwise, fills the supplied sequence [`rb`,`re`) with 32-bit quantities that depend on the sequence supplied to the constructor and possibly also depend on the history of `generate`’s previous invocations. | 𝑂(`re` - `rb`) |
| `r.size()` | `size_t` | The number of 32-bit units that would be copied by a call to `r.param`. | constant |
| `r.param(ob)` | `void` | Copies to the given destination a sequence of 32-bit units that can be provided to the constructor of a second object of type `S`, and that would reproduce in that second object a state indistinguishable from the state of the first object. | 𝑂(`r.size()`) |
