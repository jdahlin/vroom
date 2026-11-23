# 32.4.2.1 Class `thread::id` [thread.thread.id]

[Contents](../../README.md) · up: [32.4.2 Class `thread` \[thread.thread.class\]](thread.thread.class.md) · prev: [32.4.2 Class `thread` \[thread.thread.class\]](thread.thread.class.md) · next: [32.4.2.2 Constructors \[thread.thread.constr\]](thread.thread.constr.md)

```cpp
namespace std {
  class thread::id {
  public:
    id() noexcept;
  };

  bool operator==(thread::id x, thread::id y) noexcept;
  strong_ordering operator<=>(thread::id x, thread::id y) noexcept;

  template<class charT, class traits>
    basic_ostream<charT, traits>&
      operator<<(basic_ostream<charT, traits>& out, thread::id id);

  // hash support
  template<class T> struct hash;
  template<> struct hash<thread::id>;
}
```

**1** An object of type `thread::id` provides a unique identifier for each thread of execution and a single distinct value for all `thread` objects that do not represent a thread of execution ([[thread.thread.class]](thread.thread.class.md)). Each thread of execution has an associated `thread::id` object that is not equal to the `thread::id` object of any other thread of execution and that is not equal to the `thread::id` object of any `thread` object that does not represent threads of execution.

**2** `thread::id` is a trivially copyable class ([[class.prop]](../../class/class.prop.md)). The library may reuse the value of a `thread::id` of a terminated thread that can no longer be joined.

**3** [*Note 1*: Relational operators allow `thread::id` objects to be used as keys in associative containers. — *end note*]

```cpp
id() noexcept;
```

**4** *Postconditions:* The constructed object does not represent a thread of execution.

```cpp
bool operator==(thread::id x, thread::id y) noexcept;
```

**5** *Returns:* `true` only if `x` and `y` represent the same thread of execution or neither `x` nor `y` represents a thread of execution.

```cpp
strong_ordering operator<=>(thread::id x, thread::id y) noexcept;
```

**6** Let *P*(`x`, `y`) be an unspecified total ordering over `thread::id` as described in [[alg.sorting]](../algorithms/alg.sorting.md).

**7** *Returns:* `strong_ordering::less` if *P*(`x`, `y`) is `true`. Otherwise, `strong_ordering::greater` if *P*(`y`, `x`) is `true`. Otherwise, `strong_ordering::equal`.

```cpp
template<class charT, class traits>
  basic_ostream<charT, traits>&
    operator<< (basic_ostream<charT, traits>& out, thread::id id);
```

**8** *Effects:* Inserts an unspecified text representation of `id` into `out`. For two objects of type `thread::id` `x` and `y`, if `x == y` the `thread::id` objects have the same text representation and if `x != y` the `thread::id` objects have distinct text representations.

**9** *Returns:* `out`.

```cpp
template<> struct hash<thread::id>;
```

**10** The specialization is enabled ([[unord.hash]](../utilities/unord.hash.md)).
