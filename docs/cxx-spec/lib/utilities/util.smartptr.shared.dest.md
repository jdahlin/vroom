# 20.11.3.2 Destructor [util.smartptr.shared.dest]

[Contents](../../README.md) · up: [20.11.3 Class template `shared_ptr` \[util.smartptr.shared\]](util.smartptr.shared.md) · prev: [20.11.3.1 Constructors \[util.smartptr.shared.const\]](util.smartptr.shared.const.md) · next: [20.11.3.3 Assignment \[util.smartptr.shared.assign\]](util.smartptr.shared.assign.md)

```cpp
~shared_ptr();
```

**1** *Effects:*

- If `*this` is empty or shares ownership with another `shared_ptr` instance (`use_count() > 1`), there are no side effects.
- Otherwise, if `*this` owns an object `p` and a deleter `d`, `d(p)` is called.
- Otherwise, `*this` owns a pointer `p`, and `delete p` is called.

**2** [*Note 1*: Since the destruction of `*this` decreases the number of instances that share ownership with `*this` by one, after `*this` has been destroyed all `shared_ptr` instances that shared ownership with `*this` will report a `use_count()` that is one less than its previous value. — *end note*]
