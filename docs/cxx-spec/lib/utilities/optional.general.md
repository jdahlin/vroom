# 20.6.1 In general [optional.general]

[Contents](../../README.md) · up: [20.6 Optional objects \[optional\]](optional.md) · prev: [20.6 Optional objects \[optional\]](optional.md) · next: [20.6.2 Header `<optional>` synopsis \[optional.syn\]](optional.syn.md)

**1** Subclause [[optional]](optional.md) describes class template `optional` that represents optional objects. An *optional object* is an object that contains the storage for another object and manages the lifetime of this contained object, if any. The contained object may be initialized after the optional object has been initialized, and may be destroyed before the optional object has been destroyed. The initialization state of the contained object is tracked by the optional object.
