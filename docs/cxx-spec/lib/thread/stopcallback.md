# 32.3.5 Class template `stop_callback` [stopcallback]

[Contents](../../README.md) · up: [32.3 Stop tokens \[thread.stoptoken\]](thread.stoptoken.md) · prev: [32.3.4.3 Non-member functions \[stopsource.nonmembers\]](stopsource.nonmembers.md) · next: [32.3.5.1 Constructors and destructor \[stopcallback.cons\]](stopcallback.cons.md)

**1**

```cpp
namespace std {
  template<class Callback>
  class stop_callback {
  public:
    using callback_type = Callback;

    // [stopcallback.cons], constructors and destructor
    template<class C>
    explicit stop_callback(const stop_token& st, C&& cb)
        noexcept(is_nothrow_constructible_v<Callback, C>);
    template<class C>
    explicit stop_callback(stop_token&& st, C&& cb)
        noexcept(is_nothrow_constructible_v<Callback, C>);
    ~stop_callback();

    stop_callback(const stop_callback&) = delete;
    stop_callback(stop_callback&&) = delete;
    stop_callback& operator=(const stop_callback&) = delete;
    stop_callback& operator=(stop_callback&&) = delete;

  private:
    Callback callback;      // exposition only
  };

  template<class Callback>
  stop_callback(stop_token, Callback) -> stop_callback<Callback>;
}
```

**2** *Mandates:* `stop_callback` is instantiated with an argument for the template parameter `Callback` that satisfies both `invocable` and `destructible`.

**3** *Preconditions:* `stop_callback` is instantiated with an argument for the template parameter `Callback` that models both `invocable` and `destructible`.

## Subclauses

- [32.3.5.1 Constructors and destructor \[stopcallback.cons\]](stopcallback.cons.md)
