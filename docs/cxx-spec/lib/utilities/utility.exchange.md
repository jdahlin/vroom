# 20.2.3 `exchange` [utility.exchange]

[Contents](../../README.md) · up: [20.2 Utility components \[utility\]](utility.md) · prev: [20.2.2 `swap` \[utility.swap\]](utility.swap.md) · next: [20.2.4 Forward/move helpers \[forward\]](forward.md)

```cpp
template<class T, class U = T>
  constexpr T exchange(T& obj, U&& new_val);
```

**1** *Effects:* Equivalent to:

```cpp
T old_val = std::move(obj);
obj = std::forward<U>(new_val);
return old_val;
```
