# 20.12.5.2 `pool_options` data members [mem.res.pool.options]

[Contents](../../README.md) · up: [20.12.5 Pool resource classes \[mem.res.pool\]](mem.res.pool.md) · prev: [20.12.5.1 Classes `synchronized_pool_resource` and `unsynchronized_pool_resource` \[mem.res.pool.overview\]](mem.res.pool.overview.md) · next: [20.12.5.3 Constructors and destructors \[mem.res.pool.ctor\]](mem.res.pool.ctor.md)

**1** The members of `pool_options` comprise a set of constructor options for pool resources. The effect of each option on the pool resource behavior is described below:

```cpp
size_t max_blocks_per_chunk;
```

**2** The maximum number of blocks that will be allocated at once from the upstream memory resource ([[mem.res.monotonic.buffer]](mem.res.monotonic.buffer.md)) to replenish a pool. If the value of `max_blocks_per_chunk` is zero or is greater than an implementation-defined limit, that limit is used instead. The implementation may choose to use a smaller value than is specified in this field and may use different values for different pools.

```cpp
size_t largest_required_pool_block;
```

**3** The largest allocation size that is required to be fulfilled using the pooling mechanism. Attempts to allocate a single block larger than this threshold will be allocated directly from the upstream memory resource. If `largest_required_pool_block` is zero or is greater than an implementation-defined limit, that limit is used instead. The implementation may choose a pass-through threshold larger than specified in this field.
