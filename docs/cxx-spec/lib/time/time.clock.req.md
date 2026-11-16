# 27.3 *Cpp17Clock* requirements [time.clock.req]

[Contents](../../README.md) · up: [27 Time library \[time\]](time.md) · prev: [27.2 Header `<chrono>` synopsis \[time.syn\]](time.syn.md) · next: [27.4 Time-related traits \[time.traits\]](time.traits.md)

**1** A clock is a bundle consisting of a `duration`, a `time_point`, and a function `now()` to get the current `time_point`. The origin of the clock’s `time_point` is referred to as the clock’s *epoch*. A clock shall meet the requirements in [Table 97](time.clock.req.md#tab:time.clock).

**2** In [Table 97](time.clock.req.md#tab:time.clock) `C1` and `C2` denote clock types. `t1` and `t2` are values returned by `C1::now()` where the call returning `t1` happens before ([[intro.multithread]](../../basic/intro.multithread.md)) the call returning `t2` and both of these calls occur before `C1::time_point::max()`. [*Note 1*: This means `C1` did not wrap around between `t1` and `t2`. — *end note*]

<a id="tab:time.clock"></a>
**Table 97: *Cpp17Clock* requirements** [tab:time.clock]

| Expression | Return type | Operational semantics |
| --- | --- | --- |
| `C1::rep` | An arithmetic type or a class emulating an arithmetic type | The representation type of `C1::duration`. |
| `C1::period` | a specialization of `ratio` | The tick period of the clock in seconds. |
| `C1::duration` | `chrono::duration<C1::rep, C1::period>` | The `duration` type of the clock. |
| `C1::time_point` | `chrono::time_point<C1>` or `chrono::time_point<C2, C1::duration>` | The `time_point` type of the clock. `C1` and `C2` shall refer to the same epoch. |
| `C1::is_steady` | `const bool` | `true` if `t1 <= t2` is always `true` and the time between clock ticks is constant, otherwise `false`. |
| `C1::now()` | `C1::time_point` | Returns a `time_point` object representing the current point in time. |

**3** [*Note 2*: The relative difference in durations between those reported by a given clock and the SI definition is a measure of the quality of implementation. — *end note*]

**4** A type `TC` meets the *Cpp17TrivialClock* requirements if:

- `TC` meets the *Cpp17Clock* requirements ([[time.clock.req]](time.clock.req.md)),
- the types `TC::rep`, `TC::duration`, and `TC::time_point` meet the *Cpp17EqualityComparable* ([Table 25](../library/utility.arg.requirements.md#tab:cpp17.equalitycomparable)) and *Cpp17LessThanComparable* ([Table 26](../library/utility.arg.requirements.md#tab:cpp17.lessthancomparable)) requirements and the requirements of numeric types ([[numeric.requirements]](../numerics/numeric.requirements.md)). [*Note 3*: This means, in particular, that operations on these types will not throw exceptions. — *end note*]
- lvalues of the types `TC::rep`, `TC::duration`, and `TC::time_point` are swappable ([[swappable.requirements]](../library/swappable.requirements.md)),
- the function `TC::now()` does not throw exceptions, and
- the type `TC::time_point::clock` meets the *Cpp17TrivialClock* requirements, recursively.
