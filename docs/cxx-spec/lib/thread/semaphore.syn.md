# 32.7.1 Header `<semaphore>` synopsis [semaphore.syn]

[Contents](../../README.md) · up: [32.7 Semaphore \[thread.sema\]](thread.sema.md) · prev: [32.7 Semaphore \[thread.sema\]](thread.sema.md) · next: [32.7.2 Class template `counting_semaphore` \[thread.sema.cnt\]](thread.sema.cnt.md)

```cpp
namespace std {
  template<ptrdiff_t least_max_value = implementation-defined>
    class counting_semaphore;

  using binary_semaphore = counting_semaphore<1>;
}
```
