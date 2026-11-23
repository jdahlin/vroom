# 27.11.3.1 Class `nonexistent_local_time` [time.zone.exception.nonexist]

[Contents](../../README.md) · up: [27.11.3 Exception classes \[time.zone.exception\]](time.zone.exception.md) · prev: [27.11.3 Exception classes \[time.zone.exception\]](time.zone.exception.md) · next: [27.11.3.2 Class `ambiguous_local_time` \[time.zone.exception.ambig\]](time.zone.exception.ambig.md)

```cpp
namespace std::chrono {
  class nonexistent_local_time : public runtime_error {
  public:
    template<class Duration>
      nonexistent_local_time(const local_time<Duration>& tp, const local_info& i);
  };
}
```

**1** `nonexistent_local_time` is thrown when an attempt is made to convert a non-existent `local_time` to a `sys_time` without specifying `choose::earliest` or `choose::latest`.

```cpp
template<class Duration>
  nonexistent_local_time(const local_time<Duration>& tp, const local_info& i);
```

**2** *Preconditions:* `i.result == local_info::nonexistent` is `true`.

**3** *Effects:* Initializes the base class with a sequence of `char` equivalent to that produced by `os.str()` initialized as shown below:

```cpp
ostringstream os;
os << tp << " is in a gap between\n"
   << local_seconds{i.first.end.time_since_epoch()} + i.first.offset << ' '
   << i.first.abbrev << " and\n"
   << local_seconds{i.second.begin.time_since_epoch()} + i.second.offset << ' '
   << i.second.abbrev
   << " which are both equivalent to\n"
   << i.first.end << " UTC";
```

**4** [*Example 1*:

```cpp
#include <chrono>
#include <iostream>

int main() {
  using namespace std::chrono;
  try {
    auto zt = zoned_time{"America/New_York",
                         local_days{Sunday[2]/March/2016} + 2h + 30min};
  } catch (const nonexistent_local_time& e) {
    std::cout << e.what() << '\n';
  }
}
```

Produces the output:

```text
2016-03-13 02:30:00 is in a gap between
2016-03-13 02:00:00 EST and
2016-03-13 03:00:00 EDT which are both equivalent to
2016-03-13 07:00:00 UTC
```

— *end example*]
