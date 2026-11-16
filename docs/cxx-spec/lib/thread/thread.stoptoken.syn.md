# 32.3.2 Header `<stop_token>` synopsis [thread.stoptoken.syn]

[Contents](../../README.md) · up: [32.3 Stop tokens \[thread.stoptoken\]](thread.stoptoken.md) · prev: [32.3.1 Introduction \[thread.stoptoken.intro\]](thread.stoptoken.intro.md) · next: [32.3.3 Class `stop_token` \[stoptoken\]](stoptoken.md)

```cpp
namespace std {
  // [stoptoken], class stop_token
  class stop_token;

  // [stopsource], class stop_source
  class stop_source;

  // no-shared-stop-state indicator
  struct nostopstate_t {
    explicit nostopstate_t() = default;
  };
  inline constexpr nostopstate_t nostopstate{};

  // [stopcallback], class stop_callback
  template<class Callback>
  class stop_callback;
}
```
