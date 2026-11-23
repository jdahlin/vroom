# 32.2.4 Timing specifications [thread.req.timing]

[Contents](../../README.md) · up: [32.2 Requirements \[thread.req\]](thread.req.md) · prev: [32.2.3 Native handles \[thread.req.native\]](thread.req.native.md) · next: [32.2.5 Requirements for *Cpp17Lockable* types \[thread.req.lockable\]](thread.req.lockable.md)

**1** Several functions described in this Clause take an argument to specify a timeout. These timeouts are specified as either a `duration` or a `time_point` type as specified in [[time]](../time/time.md).

**2** Implementations necessarily have some delay in returning from a timeout. Any overhead in interrupt response, function return, and scheduling induces a “quality of implementation” delay, expressed as duration *D*<sub>*i*</sub>. Ideally, this delay would be zero. Further, any contention for processor and memory resources induces a “quality of management” delay, expressed as duration *D*<sub>*m*</sub>. The delay durations may vary from timeout to timeout, but in all cases shorter is better.

**3** The functions whose names end in `_for` take an argument that specifies a duration. These functions produce relative timeouts. Implementations should use a steady clock to measure time for these functions.[^1] Given a duration argument *D*<sub>*t*</sub>, the real-time duration of the timeout is *D*<sub>*t*</sub> + *D*<sub>*i*</sub> + *D*<sub>*m*</sub>.

**4** The functions whose names end in `_until` take an argument that specifies a time point. These functions produce absolute timeouts. Implementations should use the clock specified in the time point to measure time for these functions. Given a clock time point argument *C*<sub>*t*</sub>, the clock time point of the return from timeout should be *C*<sub>*t*</sub> + *D*<sub>*i*</sub> + *D*<sub>*m*</sub> when the clock is not adjusted during the timeout. If the clock is adjusted to the time *C*<sub>*a*</sub> during the timeout, the behavior should be as follows:

- if *C*<sub>*a*</sub> > *C*<sub>*t*</sub>, the waiting function should wake as soon as possible, i.e., *C*<sub>*a*</sub> + *D*<sub>*i*</sub> + *D*<sub>*m*</sub>, since the timeout is already satisfied. This specification may result in the total duration of the wait decreasing when measured against a steady clock.
- if *C*<sub>*a*</sub> ≤ *C*<sub>*t*</sub>, the waiting function should not time out until `Clock::now()` returns a time *C*<sub>*n*</sub> ≥ *C*<sub>*t*</sub>, i.e., waking at *C*<sub>*t*</sub> + *D*<sub>*i*</sub> + *D*<sub>*m*</sub>. [*Note 1*: When the clock is adjusted backwards, this specification can result in the total duration of the wait increasing when measured against a steady clock. When the clock is adjusted forwards, this specification can result in the total duration of the wait decreasing when measured against a steady clock. — *end note*]

An implementation returns from such a timeout at any point from the time specified above to the time it would return from a steady-clock relative timeout on the difference between *C*<sub>*t*</sub> and the time point of the call to the `_until` function. [*Note 2*: Implementations should decrease the duration of the wait when the clock is adjusted forwards. — *end note*]

**5** [*Note 3*: If the clock is not synchronized with a steady clock, e.g., a CPU time clock, these timeouts might not provide useful functionality. — *end note*]

**6** The resolution of timing provided by an implementation depends on both operating system and hardware. The finest resolution provided by an implementation is called the *native resolution*.

**7** Implementation-provided clocks that are used for these functions meet the *Cpp17TrivialClock* requirements ([[time.clock.req]](../time/time.clock.req.md)).

**8** A function that takes an argument which specifies a timeout will throw if, during its execution, a clock, time point, or time duration throws an exception. Such exceptions are referred to as *timeout-related exceptions*. [*Note 4*: Instantiations of clock, time point and duration types supplied by the implementation as specified in [[time.clock]](../time/time.clock.md) do not throw exceptions. — *end note*]

[^1]: All implementations for which standard time units are meaningful must necessarily have a steady clock within their hardware implementation.
