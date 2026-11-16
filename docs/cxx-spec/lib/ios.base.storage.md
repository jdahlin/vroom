# 29.5.3.5 Storage functions [ios.base.storage]

[Contents](../README.md) · up: [29.5.3 Class `ios_base` \[ios.base\]](ios.base.md) · prev: [29.5.3.4 Static members \[ios.members.static\]](ios.members.static.md) · next: [29.5.3.6 Callbacks \[ios.base.callback\]](ios.base.callback.md)

```cpp
static int xalloc();
```

**1** *Returns:* `index` `++`.

**2** *Remarks:* Concurrent access to this function by multiple threads does not result in a data race ([[intro.multithread]](../intro.multithread.md)).

```cpp
long& iword(int idx);
```

**3** *Preconditions:* `idx` is a value obtained by a call to `xalloc`.

**4** *Effects:* If `iarray` is a null pointer, allocates an array of `long` of unspecified size and stores a pointer to its first element in `iarray`. The function then extends the array pointed at by `iarray` as necessary to include the element `iarray[idx]`. Each newly allocated element of the array is initialized to zero. The reference returned is invalid after any other operations on the object.[^1] However, the value of the storage referred to is retained, so that until the next call to `copyfmt`, calling `iword` with the same index yields another reference to the same value. If the function fails[^2] and `*this` is a base class subobject of a `basic_ios<>` object or subobject, the effect is equivalent to calling `basic_ios<>::setstate(badbit)` on the derived object (which may throw `failure`).

**5** *Returns:* On success `iarray[idx]`. On failure, a valid `long&` initialized to 0.

```cpp
void*& pword(int idx);
```

**6** *Preconditions:* `idx` is a value obtained by a call to `xalloc`.

**7** *Effects:* If `parray` is a null pointer, allocates an array of pointers to `void` of unspecified size and stores a pointer to its first element in `parray`. The function then extends the array pointed at by `parray` as necessary to include the element `parray[idx]`. Each newly allocated element of the array is initialized to a null pointer. The reference returned is invalid after any other operations on the object. However, the value of the storage referred to is retained, so that until the next call to `copyfmt`, calling `pword` with the same index yields another reference to the same value. If the function fails[^3] and `*this` is a base class subobject of a `basic_ios<>` object or subobject, the effect is equivalent to calling `basic_ios<>::setstate(badbit)` on the derived object (which may throw `failure`).

**8** *Returns:* On success `parray[idx]`. On failure a valid `void*&` initialized to 0.

**9** *Remarks:* After a subsequent call to `pword(int)` for the same object, the earlier return value may no longer be valid.

[^1]: An implementation is free to implement both the integer array pointed at by `iarray` and the pointer array pointed at by `parray` as sparse data structures, possibly with a one-element cache for each.
[^2]: For example, because it cannot allocate space.
[^3]: For example, because it cannot allocate space.
