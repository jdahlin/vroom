# 27.14 Header `<ctime>` synopsis [ctime.syn]

[Contents](../README.md) · up: [27 Time library \[time\]](time.md) · prev: [27.13 Parsing \[time.parse\]](time.parse.md) · next: [28 Localization library \[localization\]](localization.md)

```cpp
#define NULL see [support.types.nullptr]
#define CLOCKS_PER_SEC see below
#define TIME_UTC see below

namespace std {
  using size_t = see [support.types.layout];
  using clock_t = see below;
  using time_t = see below;

  struct timespec;
  struct tm;

  clock_t clock();
  double difftime(time_t time1, time_t time0);
  time_t mktime(struct tm* timeptr);
  time_t time(time_t* timer);
  int timespec_get(timespec* ts, int base);
  char* asctime(const struct tm* timeptr);
  char* ctime(const time_t* timer);
  struct tm* gmtime(const time_t* timer);
  struct tm* localtime(const time_t* timer);
  size_t strftime(char* s, size_t maxsize, const char* format, const struct tm* timeptr);
}
```

**1** The contents of the header `<ctime>` are the same as the C standard library header `<time.h>`.[^1]

**2** The functions `asctime`, `ctime`, `gmtime`, and `localtime` are not required to avoid data races ([[res.on.data.races]](res.on.data.races.md)).

*See also:* ISO C 7.27

[^1]: `strftime` supports the C conversion specifiers `C`, `D`, `e`, `F`, `g`, `G`, `h`, `r`, `R`, `t`, `T`, `u`, `V`, and `z`, and the modifiers `E` and `O`.
