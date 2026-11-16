# 20.12.5.3 Constructors and destructors [mem.res.pool.ctor]

[Contents](../README.md) · up: [20.12.5 Pool resource classes \[mem.res.pool\]](mem.res.pool.md) · prev: [20.12.5.2 `pool_options` data members \[mem.res.pool.options\]](mem.res.pool.options.md) · next: [20.12.5.4 Members \[mem.res.pool.mem\]](mem.res.pool.mem.md)

```cpp
synchronized_pool_resource(const pool_options& opts, memory_resource* upstream);
unsynchronized_pool_resource(const pool_options& opts, memory_resource* upstream);
```

**1** *Preconditions:* `upstream` is the address of a valid memory resource.

**2** *Effects:* Constructs a pool resource object that will obtain memory from `upstream` whenever the pool resource is unable to satisfy a memory request from its own internal data structures. The resulting object will hold a copy of `upstream`, but will not own the resource to which `upstream` points. [*Note 1*: The intention is that calls to `upstream->allocate()` will be substantially fewer than calls to `this->allocate()` in most cases. — *end note*] The behavior of the pooling mechanism is tuned according to the value of the `opts` argument.

**3** *Throws:* Nothing unless `upstream->allocate()` throws. It is unspecified if, or under what conditions, this constructor calls `upstream->allocate()`.

```cpp
virtual ~synchronized_pool_resource();
virtual ~unsynchronized_pool_resource();
```

**4** *Effects:* Calls `release()`.
