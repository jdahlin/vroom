# 16.5.4.10 Library object access [res.on.objects]

[Contents](../README.md) · up: [16.5.4 Constraints on programs \[constraints\]](constraints.md) · prev: [16.5.4.9 Function arguments \[res.on.arguments\]](res.on.arguments.md) · next: [16.5.4.11 Expects paragraph \[res.on.expects\]](res.on.expects.md)

**1** The behavior of a program is undefined if calls to standard library functions from different threads may introduce a data race. The conditions under which this may occur are specified in [[res.on.data.races]](res.on.data.races.md). [*Note 1*: Modifying an object of a standard library type that is shared between threads risks undefined behavior unless objects of that type are explicitly specified as being shareable without data races or the user supplies a locking mechanism. — *end note*]

**2** If an object of a standard library type is accessed, and the beginning of the object’s lifetime ([[basic.life]](../basic.life.md)) does not happen before the access, or the access does not happen before the end of the object’s lifetime, the behavior is undefined unless otherwise specified. [*Note 2*: This applies even to objects such as mutexes intended for thread synchronization. — *end note*]
