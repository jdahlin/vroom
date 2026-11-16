# 29.5.3.7 Constructors and destructor [ios.base.cons]

[Contents](../README.md) · up: [29.5.3 Class `ios_base` \[ios.base\]](ios.base.md) · prev: [29.5.3.6 Callbacks \[ios.base.callback\]](ios.base.callback.md) · next: [29.5.4 Class template `fpos` \[fpos\]](fpos.md)

```cpp
ios_base();
```

**1** *Effects:* Each `ios_base` member has an indeterminate value after construction. The object’s members shall be initialized by calling `basic_ios::init` before the object’s first use or before it is destroyed, whichever comes first; otherwise the behavior is undefined.

```cpp
~ios_base();
```

**2** *Effects:* Calls each registered callback pair `(fn, idx)` ([[ios.base.callback]](ios.base.callback.md)) as `(*fn)(erase_event, *this, idx)` at such time that any `ios_base` member function called from within `fn` has well-defined results.
