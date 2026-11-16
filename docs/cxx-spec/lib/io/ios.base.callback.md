# 29.5.3.6 Callbacks [ios.base.callback]

[Contents](../../README.md) · up: [29.5.3 Class `ios_base` \[ios.base\]](ios.base.md) · prev: [29.5.3.5 Storage functions \[ios.base.storage\]](ios.base.storage.md) · next: [29.5.3.7 Constructors and destructor \[ios.base.cons\]](ios.base.cons.md)

```cpp
void register_callback(event_callback fn, int idx);
```

**1** *Preconditions:* The function `fn` does not throw exceptions.

**2** *Effects:* Registers the pair `(fn, idx)` such that during calls to `imbue()` ([[ios.base.locales]](ios.base.locales.md)), `copyfmt()`, or `~ios_base()` ([[ios.base.cons]](ios.base.cons.md)), the function `fn` is called with argument `idx`. Functions registered are called when an event occurs, in opposite order of registration. Functions registered while a callback function is active are not called until the next event.

**3** *Remarks:* Identical pairs are not merged. A function registered twice will be called twice.
