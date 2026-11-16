# 32.8.1 Latches [thread.latch]

[Contents](../README.md) · up: [32.8 Coordination types \[thread.coord\]](thread.coord.md) · prev: [32.8 Coordination types \[thread.coord\]](thread.coord.md) · next: [32.8.1.1 Header `<latch>` synopsis \[latch.syn\]](latch.syn.md)

**1** A latch is a thread coordination mechanism that allows any number of threads to block until an expected number of threads arrive at the latch (via the `count_down` function). The expected count is set when the latch is created. An individual latch is a single-use object; once the expected count has been reached, the latch cannot be reused.

## Subclauses

- [32.8.1.1 Header `<latch>` synopsis \[latch.syn\]](latch.syn.md)
- [32.8.1.2 Class `latch` \[thread.latch.class\]](thread.latch.class.md)
