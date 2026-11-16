# 27.11.3.2 Class `ambiguous_local_time` [time.zone.exception.ambig]

[Contents](../README.md) · up: [27.11.3 Exception classes \[time.zone.exception\]](time.zone.exception.md) · prev: [27.11.3.1 Class `nonexistent_local_time` \[time.zone.exception.nonexist\]](time.zone.exception.nonexist.md) · next: [27.11.4 Information classes \[time.zone.info\]](time.zone.info.md)

```cpp
namespace std::chrono {
  class ambiguous_local_time : public runtime_error {
  public:
    template<class Duration>
      ambiguous_local_time(const local_time<Duration>& tp, const local_info& i);
  };
}
```

**1** `ambiguous_local_time` is thrown when an attempt is made to convert an ambiguous `local_time` to a `sys_time` without specifying `choose::earliest` or `choose::latest`.

```cpp
template<class Duration>
  ambiguous_local_time(const local_time<Duration>& tp, const local_info& i);
```

**2** *Preconditions:* `i.result == local_info::ambiguous` is `true`.

**3** *Effects:* Initializes the base class with a sequence of `char` equivalent to that produced by `os.str()` initialized as shown below:

```cpp
ostringstream os;
os << tp << " is ambiguous.  It could be\n"
   << tp << ' ' << i.first.abbrev << " == "
   << tp - i.first.offset << " UTC or\n"
   << tp << ' ' << i.second.abbrev  << " == "
   << tp - i.second.offset  << " UTC";
```

**4** [*Example 1*:

```cpp
#include <chrono>
#include <iostream>

int main() {
  using namespace std::chrono;
  try {
    auto zt = zoned_time{"America/New_York",
                         local_days{Sunday[1]/November/2016} + 1h + 30min};
  } catch (const ambiguous_local_time& e) {
    std::cout << e.what() << '\n';
  }
}
```

Produces the output:

```text
2016-11-06 01:30:00 is ambiguous.  It could be
2016-11-06 01:30:00 EDT == 2016-11-06 05:30:00 UTC or
2016-11-06 01:30:00 EST == 2016-11-06 06:30:00 UTC
```

— *end example*]
