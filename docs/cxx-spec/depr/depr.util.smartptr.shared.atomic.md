# D.18 Deprecated `shared_ptr` atomic access [depr.util.smartptr.shared.atomic]

[Contents](../README.md) · up: [Annex D Compatibility features \[depr\]](depr.md) · prev: [D.17 Deprecated `move_iterator` access \[depr.move.iter.elem\]](depr.move.iter.elem.md) · next: [D.19 Deprecated `basic_string` capacity \[depr.string.capacity\]](depr.string.capacity.md)

**1** The header `<memory>` ([[memory.syn]](../lib/utilities/memory.syn.md)) has the following additions:

```cpp
namespace std {
  template<class T>
    bool atomic_is_lock_free(const shared_ptr<T>* p);

  template<class T>
    shared_ptr<T> atomic_load(const shared_ptr<T>* p);
  template<class T>
    shared_ptr<T> atomic_load_explicit(const shared_ptr<T>* p, memory_order mo);

  template<class T>
    void atomic_store(shared_ptr<T>* p, shared_ptr<T> r);
  template<class T>
    void atomic_store_explicit(shared_ptr<T>* p, shared_ptr<T> r, memory_order mo);

  template<class T>
    shared_ptr<T> atomic_exchange(shared_ptr<T>* p, shared_ptr<T> r);
  template<class T>
    shared_ptr<T> atomic_exchange_explicit(shared_ptr<T>* p, shared_ptr<T> r, memory_order mo);

  template<class T>
    bool atomic_compare_exchange_weak(shared_ptr<T>* p, shared_ptr<T>* v, shared_ptr<T> w);
  template<class T>
    bool atomic_compare_exchange_strong(shared_ptr<T>* p, shared_ptr<T>* v, shared_ptr<T> w);
  template<class T>
    bool atomic_compare_exchange_weak_explicit(
      shared_ptr<T>* p, shared_ptr<T>* v, shared_ptr<T> w,
      memory_order success, memory_order failure);
  template<class T>
    bool atomic_compare_exchange_strong_explicit(
      shared_ptr<T>* p, shared_ptr<T>* v, shared_ptr<T> w,
      memory_order success, memory_order failure);
}
```

**2** Concurrent access to a `shared_ptr` object from multiple threads does not introduce a data race if the access is done exclusively via the functions in this subclause and the instance is passed as their first argument.

**3** The meaning of the arguments of type `memory_order` is explained in [[atomics.order]](../lib/atomics/atomics.order.md).

```cpp
template<class T> bool atomic_is_lock_free(const shared_ptr<T>* p);
```

**4** *Requires:* `p` shall not be null.

**5** *Returns:* `true` if atomic access to `*p` is lock-free, `false` otherwise.

**6** *Throws:* Nothing.

```cpp
template<class T> shared_ptr<T> atomic_load(const shared_ptr<T>* p);
```

**7** *Requires:* `p` shall not be null.

**8** *Returns:* `atomic_load_explicit(p, memory_order::seq_cst)`.

**9** *Throws:* Nothing.

```cpp
template<class T> shared_ptr<T> atomic_load_explicit(const shared_ptr<T>* p, memory_order mo);
```

**10** *Requires:* `p` shall not be null.

**11** *Requires:* `mo` shall not be `memory_order::release` or `memory_order::acq_rel`.

**12** *Returns:* `*p`.

**13** *Throws:* Nothing.

```cpp
template<class T> void atomic_store(shared_ptr<T>* p, shared_ptr<T> r);
```

**14** *Requires:* `p` shall not be null.

**15** *Effects:* As if by `atomic_store_explicit(p, r, memory_order::seq_cst)`.

**16** *Throws:* Nothing.

```cpp
template<class T> void atomic_store_explicit(shared_ptr<T>* p, shared_ptr<T> r, memory_order mo);
```

**17** *Requires:* `p` shall not be null.

**18** *Requires:* `mo` shall not be `memory_order::acquire` or `memory_order::acq_rel`.

**19** *Effects:* As if by `p->swap(r)`.

**20** *Throws:* Nothing.

```cpp
template<class T> shared_ptr<T> atomic_exchange(shared_ptr<T>* p, shared_ptr<T> r);
```

**21** *Requires:* `p` shall not be null.

**22** *Returns:* `atomic_exchange_explicit(p, r, memory_order::seq_cst)`.

**23** *Throws:* Nothing.

```cpp
template<class T>
  shared_ptr<T> atomic_exchange_explicit(shared_ptr<T>* p, shared_ptr<T> r, memory_order mo);
```

**24** *Requires:* `p` shall not be null.

**25** *Effects:* As if by `p->swap(r)`.

**26** *Returns:* The previous value of `*p`.

**27** *Throws:* Nothing.

```cpp
template<class T>
  bool atomic_compare_exchange_weak(shared_ptr<T>* p, shared_ptr<T>* v, shared_ptr<T> w);
```

**28** *Requires:* `p` shall not be null and `v` shall not be null.

**29** *Returns:*

```cpp
atomic_compare_exchange_weak_explicit(p, v, w, memory_order::seq_cst, memory_order::seq_cst)
```

**30** *Throws:* Nothing.

```cpp
template<class T>
  bool atomic_compare_exchange_strong(shared_ptr<T>* p, shared_ptr<T>* v, shared_ptr<T> w);
```

**31** *Returns:*

```cpp
atomic_compare_exchange_strong_explicit(p, v, w, memory_order::seq_cst,
                                        memory_order::seq_cst)
```

```cpp
template<class T>
  bool atomic_compare_exchange_weak_explicit(
    shared_ptr<T>* p, shared_ptr<T>* v, shared_ptr<T> w,
    memory_order success, memory_order failure);
template<class T>
  bool atomic_compare_exchange_strong_explicit(
    shared_ptr<T>* p, shared_ptr<T>* v, shared_ptr<T> w,
    memory_order success, memory_order failure);
```

**32** *Requires:* `p` shall not be null and `v` shall not be null. The `failure` argument shall not be `memory_order::release` nor `memory_order::acq_rel`.

**33** *Effects:* If `*p` is equivalent to `*v`, assigns `w` to `*p` and has synchronization semantics corresponding to the value of `success`, otherwise assigns `*p` to `*v` and has synchronization semantics corresponding to the value of `failure`.

**34** *Returns:* `true` if `*p` was equivalent to `*v`, `false` otherwise.

**35** *Throws:* Nothing.

**36** *Remarks:* Two `shared_ptr` objects are equivalent if they store the same pointer value and share ownership. The weak form may fail spuriously. See [[atomics.types.operations]](../lib/atomics/atomics.types.operations.md).
