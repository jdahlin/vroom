# 27.2 Header `<chrono>` synopsis [time.syn]

[Contents](../../README.md) · up: [27 Time library \[time\]](time.md) · prev: [27.1 General \[time.general\]](time.general.md) · next: [27.3 *Cpp17Clock* requirements \[time.clock.req\]](time.clock.req.md)

```cpp
#include <compare>              // see [compare.syn]

namespace std {
  namespace chrono {
    // [time.duration], class template duration
    template<class Rep, class Period = ratio<1>> class duration;

    // [time.point], class template time_point
    template<class Clock, class Duration = typename Clock::duration> class time_point;
  }

  // [time.traits.specializations], common_type specializations
  template<class Rep1, class Period1, class Rep2, class Period2>
    struct common_type<chrono::duration<Rep1, Period1>,
                       chrono::duration<Rep2, Period2>>;

  template<class Clock, class Duration1, class Duration2>
    struct common_type<chrono::time_point<Clock, Duration1>,
                       chrono::time_point<Clock, Duration2>>;

  namespace chrono {
    // [time.traits], customization traits
    template<class Rep> struct treat_as_floating_point;
    template<class Rep>
      inline constexpr bool treat_as_floating_point_v = treat_as_floating_point<Rep>::value;

    template<class Rep> struct duration_values;

    template<class T> struct is_clock;
    template<class T> inline constexpr bool is_clock_v = is_clock<T>::value;

    // [time.duration.nonmember], duration arithmetic
    template<class Rep1, class Period1, class Rep2, class Period2>
      constexpr common_type_t<duration<Rep1, Period1>, duration<Rep2, Period2>>
        operator+(const duration<Rep1, Period1>& lhs, const duration<Rep2, Period2>& rhs);
    template<class Rep1, class Period1, class Rep2, class Period2>
      constexpr common_type_t<duration<Rep1, Period1>, duration<Rep2, Period2>>
        operator-(const duration<Rep1, Period1>& lhs, const duration<Rep2, Period2>& rhs);
    template<class Rep1, class Period, class Rep2>
      constexpr duration<common_type_t<Rep1, Rep2>, Period>
        operator*(const duration<Rep1, Period>& d, const Rep2& s);
    template<class Rep1, class Rep2, class Period>
      constexpr duration<common_type_t<Rep1, Rep2>, Period>
        operator*(const Rep1& s, const duration<Rep2, Period>& d);
    template<class Rep1, class Period, class Rep2>
      constexpr duration<common_type_t<Rep1, Rep2>, Period>
        operator/(const duration<Rep1, Period>& d, const Rep2& s);
    template<class Rep1, class Period1, class Rep2, class Period2>
      constexpr common_type_t<Rep1, Rep2>
        operator/(const duration<Rep1, Period1>& lhs, const duration<Rep2, Period2>& rhs);
    template<class Rep1, class Period, class Rep2>
      constexpr duration<common_type_t<Rep1, Rep2>, Period>
        operator%(const duration<Rep1, Period>& d, const Rep2& s);
    template<class Rep1, class Period1, class Rep2, class Period2>
      constexpr common_type_t<duration<Rep1, Period1>, duration<Rep2, Period2>>
        operator%(const duration<Rep1, Period1>& lhs, const duration<Rep2, Period2>& rhs);

    // [time.duration.comparisons], duration comparisons
    template<class Rep1, class Period1, class Rep2, class Period2>
      constexpr bool operator==(const duration<Rep1, Period1>& lhs,
                                const duration<Rep2, Period2>& rhs);
    template<class Rep1, class Period1, class Rep2, class Period2>
      constexpr bool operator< (const duration<Rep1, Period1>& lhs,
                                const duration<Rep2, Period2>& rhs);
    template<class Rep1, class Period1, class Rep2, class Period2>
      constexpr bool operator> (const duration<Rep1, Period1>& lhs,
                                const duration<Rep2, Period2>& rhs);
    template<class Rep1, class Period1, class Rep2, class Period2>
      constexpr bool operator<=(const duration<Rep1, Period1>& lhs,
                                const duration<Rep2, Period2>& rhs);
    template<class Rep1, class Period1, class Rep2, class Period2>
      constexpr bool operator>=(const duration<Rep1, Period1>& lhs,
                                const duration<Rep2, Period2>& rhs);
    template<class Rep1, class Period1, class Rep2, class Period2>
      requires see below
      constexpr auto operator<=>(const duration<Rep1, Period1>& lhs,
                                 const duration<Rep2, Period2>& rhs);

    // [time.duration.cast], conversions
    template<class ToDuration, class Rep, class Period>
      constexpr ToDuration duration_cast(const duration<Rep, Period>& d);
    template<class ToDuration, class Rep, class Period>
      constexpr ToDuration floor(const duration<Rep, Period>& d);
    template<class ToDuration, class Rep, class Period>
      constexpr ToDuration ceil(const duration<Rep, Period>& d);
    template<class ToDuration, class Rep, class Period>
      constexpr ToDuration round(const duration<Rep, Period>& d);

    // [time.duration.io], duration I/O
    template<class charT, class traits, class Rep, class Period>
      basic_ostream<charT, traits>&
        operator<<(basic_ostream<charT, traits>& os,
                   const duration<Rep, Period>& d);
    template<class charT, class traits, class Rep, class Period, class Alloc = allocator<charT>>
      basic_istream<charT, traits>&
        from_stream(basic_istream<charT, traits>& is, const charT* fmt,
                    duration<Rep, Period>& d,
                    basic_string<charT, traits, Alloc>* abbrev = nullptr,
                    minutes* offset = nullptr);

    // convenience typedefs
    using nanoseconds  = duration<signed integer type of at least 64 bits, nano>;
    using microseconds = duration<signed integer type of at least 55 bits, micro>;
    using milliseconds = duration<signed integer type of at least 45 bits, milli>;
    using seconds      = duration<signed integer type of at least 35 bits>;
    using minutes      = duration<signed integer type of at least 29 bits, ratio<  60>>;
    using hours        = duration<signed integer type of at least 23 bits, ratio<3600>>;
    using days         = duration<signed integer type of at least 25 bits,
                                  ratio_multiply<ratio<24>, hours::period>>;
    using weeks        = duration<signed integer type of at least 22 bits,
                                  ratio_multiply<ratio<7>, days::period>>;
    using years        = duration<signed integer type of at least 17 bits,
                                  ratio_multiply<ratio<146097, 400>, days::period>>;
    using months       = duration<signed integer type of at least 20 bits,
                                  ratio_divide<years::period, ratio<12>>>;

    // [time.point.nonmember], time_point arithmetic
    template<class Clock, class Duration1, class Rep2, class Period2>
      constexpr time_point<Clock, common_type_t<Duration1, duration<Rep2, Period2>>>
        operator+(const time_point<Clock, Duration1>& lhs, const duration<Rep2, Period2>& rhs);
    template<class Rep1, class Period1, class Clock, class Duration2>
      constexpr time_point<Clock, common_type_t<duration<Rep1, Period1>, Duration2>>
        operator+(const duration<Rep1, Period1>& lhs, const time_point<Clock, Duration2>& rhs);
    template<class Clock, class Duration1, class Rep2, class Period2>
      constexpr time_point<Clock, common_type_t<Duration1, duration<Rep2, Period2>>>
        operator-(const time_point<Clock, Duration1>& lhs, const duration<Rep2, Period2>& rhs);
    template<class Clock, class Duration1, class Duration2>
      constexpr common_type_t<Duration1, Duration2>
        operator-(const time_point<Clock, Duration1>& lhs,
                  const time_point<Clock, Duration2>& rhs);

    // [time.point.comparisons], time_point comparisons
    template<class Clock, class Duration1, class Duration2>
       constexpr bool operator==(const time_point<Clock, Duration1>& lhs,
                                 const time_point<Clock, Duration2>& rhs);
    template<class Clock, class Duration1, class Duration2>
       constexpr bool operator< (const time_point<Clock, Duration1>& lhs,
                                 const time_point<Clock, Duration2>& rhs);
    template<class Clock, class Duration1, class Duration2>
       constexpr bool operator> (const time_point<Clock, Duration1>& lhs,
                                 const time_point<Clock, Duration2>& rhs);
    template<class Clock, class Duration1, class Duration2>
       constexpr bool operator<=(const time_point<Clock, Duration1>& lhs,
                                 const time_point<Clock, Duration2>& rhs);
    template<class Clock, class Duration1, class Duration2>
       constexpr bool operator>=(const time_point<Clock, Duration1>& lhs,
                                 const time_point<Clock, Duration2>& rhs);
    template<class Clock, class Duration1, three_way_comparable_with<Duration1> Duration2>
       constexpr auto operator<=>(const time_point<Clock, Duration1>& lhs,
                                  const time_point<Clock, Duration2>& rhs);

    // [time.point.cast], conversions
    template<class ToDuration, class Clock, class Duration>
      constexpr time_point<Clock, ToDuration>
        time_point_cast(const time_point<Clock, Duration>& t);
    template<class ToDuration, class Clock, class Duration>
      constexpr time_point<Clock, ToDuration> floor(const time_point<Clock, Duration>& tp);
    template<class ToDuration, class Clock, class Duration>
      constexpr time_point<Clock, ToDuration> ceil(const time_point<Clock, Duration>& tp);
    template<class ToDuration, class Clock, class Duration>
      constexpr time_point<Clock, ToDuration> round(const time_point<Clock, Duration>& tp);

    // [time.duration.alg], specialized algorithms
    template<class Rep, class Period>
      constexpr duration<Rep, Period> abs(duration<Rep, Period> d);

    // [time.clock.system], class system_clock
    class system_clock;

    template<class Duration>
      using sys_time  = time_point<system_clock, Duration>;
    using sys_seconds = sys_time<seconds>;
    using sys_days    = sys_time<days>;

    template<class charT, class traits, class Duration>
      basic_ostream<charT, traits>&
        operator<<(basic_ostream<charT, traits>& os, const sys_time<Duration>& tp);

    template<class charT, class traits>
      basic_ostream<charT, traits>&
        operator<<(basic_ostream<charT, traits>& os, const sys_days& dp);

    template<class charT, class traits, class Duration, class Alloc = allocator<charT>>
      basic_istream<charT, traits>&
        from_stream(basic_istream<charT, traits>& is, const charT* fmt,
                    sys_time<Duration>& tp,
                    basic_string<charT, traits, Alloc>* abbrev = nullptr,
                    minutes* offset = nullptr);

    // [time.clock.utc], class utc_clock
    class utc_clock;

    template<class Duration>
      using utc_time  = time_point<utc_clock, Duration>;
    using utc_seconds = utc_time<seconds>;

    template<class charT, class traits, class Duration>
      basic_ostream<charT, traits>&
        operator<<(basic_ostream<charT, traits>& os, const utc_time<Duration>& t);
    template<class charT, class traits, class Duration, class Alloc = allocator<charT>>
      basic_istream<charT, traits>&
        from_stream(basic_istream<charT, traits>& is, const charT* fmt,
                    utc_time<Duration>& tp,
                    basic_string<charT, traits, Alloc>* abbrev = nullptr,
                    minutes* offset = nullptr);

    struct leap_second_info;

    template<class Duration>
      leap_second_info get_leap_second_info(const utc_time<Duration>& ut);

    // [time.clock.tai], class tai_clock
    class tai_clock;

    template<class Duration>
      using tai_time  = time_point<tai_clock, Duration>;
    using tai_seconds = tai_time<seconds>;

    template<class charT, class traits, class Duration>
      basic_ostream<charT, traits>&
        operator<<(basic_ostream<charT, traits>& os, const tai_time<Duration>& t);
    template<class charT, class traits, class Duration, class Alloc = allocator<charT>>
      basic_istream<charT, traits>&
        from_stream(basic_istream<charT, traits>& is, const charT* fmt,
                    tai_time<Duration>& tp,
                    basic_string<charT, traits, Alloc>* abbrev = nullptr,
                    minutes* offset = nullptr);

    // [time.clock.gps], class gps_clock
    class gps_clock;

    template<class Duration>
      using gps_time  = time_point<gps_clock, Duration>;
    using gps_seconds = gps_time<seconds>;

    template<class charT, class traits, class Duration>
      basic_ostream<charT, traits>&
        operator<<(basic_ostream<charT, traits>& os, const gps_time<Duration>& t);
    template<class charT, class traits, class Duration, class Alloc = allocator<charT>>
      basic_istream<charT, traits>&
        from_stream(basic_istream<charT, traits>& is, const charT* fmt,
                    gps_time<Duration>& tp,
                    basic_string<charT, traits, Alloc>* abbrev = nullptr,
                    minutes* offset = nullptr);

    // [time.clock.file], type file_clock
    using file_clock = see below;

    template<class Duration>
      using file_time = time_point<file_clock, Duration>;

    template<class charT, class traits, class Duration>
      basic_ostream<charT, traits>&
        operator<<(basic_ostream<charT, traits>& os, const file_time<Duration>& tp);
    template<class charT, class traits, class Duration, class Alloc = allocator<charT>>
      basic_istream<charT, traits>&
        from_stream(basic_istream<charT, traits>& is, const charT* fmt,
                    file_time<Duration>& tp,
                    basic_string<charT, traits, Alloc>* abbrev = nullptr,
                    minutes* offset = nullptr);

    // [time.clock.steady], class steady_clock
    class steady_clock;

    // [time.clock.hires], class high_resolution_clock
    class high_resolution_clock;

    // [time.clock.local], local time
    struct local_t {};
    template<class Duration>
      using local_time  = time_point<local_t, Duration>;
    using local_seconds = local_time<seconds>;
    using local_days    = local_time<days>;

    template<class charT, class traits, class Duration>
      basic_ostream<charT, traits>&
        operator<<(basic_ostream<charT, traits>& os, const local_time<Duration>& tp);
    template<class charT, class traits, class Duration, class Alloc = allocator<charT>>
      basic_istream<charT, traits>&
        from_stream(basic_istream<charT, traits>& is, const charT* fmt,
                    local_time<Duration>& tp,
                    basic_string<charT, traits, Alloc>* abbrev = nullptr,
                    minutes* offset = nullptr);

    // [time.clock.cast], time_point conversions
    template<class DestClock, class SourceClock>
      struct clock_time_conversion;

    template<class DestClock, class SourceClock, class Duration>
      auto clock_cast(const time_point<SourceClock, Duration>& t);

    // [time.cal.last], class last_spec
    struct last_spec;

    // [time.cal.day], class day
    class day;

    constexpr bool operator==(const day& x, const day& y) noexcept;
    constexpr strong_ordering operator<=>(const day& x, const day& y) noexcept;

    constexpr day  operator+(const day&  x, const days& y) noexcept;
    constexpr day  operator+(const days& x, const day&  y) noexcept;
    constexpr day  operator-(const day&  x, const days& y) noexcept;
    constexpr days operator-(const day&  x, const day&  y) noexcept;

    template<class charT, class traits>
      basic_ostream<charT, traits>&
        operator<<(basic_ostream<charT, traits>& os, const day& d);
    template<class charT, class traits, class Alloc = allocator<charT>>
      basic_istream<charT, traits>&
        from_stream(basic_istream<charT, traits>& is, const charT* fmt,
                    day& d, basic_string<charT, traits, Alloc>* abbrev = nullptr,
                    minutes* offset = nullptr);

    // [time.cal.month], class month
    class month;

    constexpr bool operator==(const month& x, const month& y) noexcept;
    constexpr strong_ordering operator<=>(const month& x, const month& y) noexcept;

    constexpr month  operator+(const month&  x, const months& y) noexcept;
    constexpr month  operator+(const months& x,  const month& y) noexcept;
    constexpr month  operator-(const month&  x, const months& y) noexcept;
    constexpr months operator-(const month&  x,  const month& y) noexcept;

    template<class charT, class traits>
      basic_ostream<charT, traits>&
        operator<<(basic_ostream<charT, traits>& os, const month& m);
    template<class charT, class traits, class Alloc = allocator<charT>>
      basic_istream<charT, traits>&
        from_stream(basic_istream<charT, traits>& is, const charT* fmt,
                    month& m, basic_string<charT, traits, Alloc>* abbrev = nullptr,
                    minutes* offset = nullptr);

    // [time.cal.year], class year
    class year;

    constexpr bool operator==(const year& x, const year& y) noexcept;
    constexpr strong_ordering operator<=>(const year& x, const year& y) noexcept;

    constexpr year  operator+(const year&  x, const years& y) noexcept;
    constexpr year  operator+(const years& x, const year&  y) noexcept;
    constexpr year  operator-(const year&  x, const years& y) noexcept;
    constexpr years operator-(const year&  x, const year&  y) noexcept;

    template<class charT, class traits>
      basic_ostream<charT, traits>&
        operator<<(basic_ostream<charT, traits>& os, const year& y);

    template<class charT, class traits, class Alloc = allocator<charT>>
      basic_istream<charT, traits>&
        from_stream(basic_istream<charT, traits>& is, const charT* fmt,
                    year& y, basic_string<charT, traits, Alloc>* abbrev = nullptr,
                    minutes* offset = nullptr);

    // [time.cal.wd], class weekday
    class weekday;

    constexpr bool operator==(const weekday& x, const weekday& y) noexcept;

    constexpr weekday operator+(const weekday& x, const days&    y) noexcept;
    constexpr weekday operator+(const days&    x, const weekday& y) noexcept;
    constexpr weekday operator-(const weekday& x, const days&    y) noexcept;
    constexpr days    operator-(const weekday& x, const weekday& y) noexcept;

    template<class charT, class traits>
      basic_ostream<charT, traits>&
        operator<<(basic_ostream<charT, traits>& os, const weekday& wd);

    template<class charT, class traits, class Alloc = allocator<charT>>
      basic_istream<charT, traits>&
        from_stream(basic_istream<charT, traits>& is, const charT* fmt,
                    weekday& wd, basic_string<charT, traits, Alloc>* abbrev = nullptr,
                    minutes* offset = nullptr);

    // [time.cal.wdidx], class weekday_indexed
    class weekday_indexed;

    constexpr bool operator==(const weekday_indexed& x, const weekday_indexed& y) noexcept;

    template<class charT, class traits>
      basic_ostream<charT, traits>&
        operator<<(basic_ostream<charT, traits>& os, const weekday_indexed& wdi);

    // [time.cal.wdlast], class weekday_last
    class weekday_last;

    constexpr bool operator==(const weekday_last& x, const weekday_last& y) noexcept;

    template<class charT, class traits>
      basic_ostream<charT, traits>&
        operator<<(basic_ostream<charT, traits>& os, const weekday_last& wdl);

    // [time.cal.md], class month_day
    class month_day;

    constexpr bool operator==(const month_day& x, const month_day& y) noexcept;
    constexpr strong_ordering operator<=>(const month_day& x, const month_day& y) noexcept;

    template<class charT, class traits>
      basic_ostream<charT, traits>&
        operator<<(basic_ostream<charT, traits>& os, const month_day& md);

    template<class charT, class traits, class Alloc = allocator<charT>>
      basic_istream<charT, traits>&
        from_stream(basic_istream<charT, traits>& is, const charT* fmt,
                    month_day& md, basic_string<charT, traits, Alloc>* abbrev = nullptr,
                    minutes* offset = nullptr);

    // [time.cal.mdlast], class month_day_last
    class month_day_last;

    constexpr bool operator==(const month_day_last& x, const month_day_last& y) noexcept;
    constexpr strong_ordering operator<=>(const month_day_last& x,
                                          const month_day_last& y) noexcept;

    template<class charT, class traits>
      basic_ostream<charT, traits>&
        operator<<(basic_ostream<charT, traits>& os, const month_day_last& mdl);

    // [time.cal.mwd], class month_weekday
    class month_weekday;

    constexpr bool operator==(const month_weekday& x, const month_weekday& y) noexcept;

    template<class charT, class traits>
      basic_ostream<charT, traits>&
        operator<<(basic_ostream<charT, traits>& os, const month_weekday& mwd);

    // [time.cal.mwdlast], class month_weekday_last
    class month_weekday_last;

    constexpr bool operator==(const month_weekday_last& x, const month_weekday_last& y) noexcept;

    template<class charT, class traits>
      basic_ostream<charT, traits>&
        operator<<(basic_ostream<charT, traits>& os, const month_weekday_last& mwdl);

    // [time.cal.ym], class year_month
    class year_month;

    constexpr bool operator==(const year_month& x, const year_month& y) noexcept;
    constexpr strong_ordering operator<=>(const year_month& x, const year_month& y) noexcept;

    constexpr year_month operator+(const year_month& ym, const months& dm) noexcept;
    constexpr year_month operator+(const months& dm, const year_month& ym) noexcept;
    constexpr year_month operator-(const year_month& ym, const months& dm) noexcept;
    constexpr months operator-(const year_month& x, const year_month& y) noexcept;
    constexpr year_month operator+(const year_month& ym, const years& dy) noexcept;
    constexpr year_month operator+(const years& dy, const year_month& ym) noexcept;
    constexpr year_month operator-(const year_month& ym, const years& dy) noexcept;

    template<class charT, class traits>
      basic_ostream<charT, traits>&
        operator<<(basic_ostream<charT, traits>& os, const year_month& ym);

    template<class charT, class traits, class Alloc = allocator<charT>>
      basic_istream<charT, traits>&
        from_stream(basic_istream<charT, traits>& is, const charT* fmt,
                    year_month& ym, basic_string<charT, traits, Alloc>* abbrev = nullptr,
                    minutes* offset = nullptr);

    // [time.cal.ymd], class year_month_day
    class year_month_day;

    constexpr bool operator==(const year_month_day& x, const year_month_day& y) noexcept;
    constexpr strong_ordering operator<=>(const year_month_day& x,
                                          const year_month_day& y) noexcept;

    constexpr year_month_day operator+(const year_month_day& ymd, const months& dm) noexcept;
    constexpr year_month_day operator+(const months& dm, const year_month_day& ymd) noexcept;
    constexpr year_month_day operator+(const year_month_day& ymd, const years& dy) noexcept;
    constexpr year_month_day operator+(const years& dy, const year_month_day& ymd) noexcept;
    constexpr year_month_day operator-(const year_month_day& ymd, const months& dm) noexcept;
    constexpr year_month_day operator-(const year_month_day& ymd, const years& dy) noexcept;

    template<class charT, class traits>
      basic_ostream<charT, traits>&
        operator<<(basic_ostream<charT, traits>& os, const year_month_day& ymd);

    template<class charT, class traits, class Alloc = allocator<charT>>
      basic_istream<charT, traits>&
        from_stream(basic_istream<charT, traits>& is, const charT* fmt,
                    year_month_day& ymd,
                    basic_string<charT, traits, Alloc>* abbrev = nullptr,
                    minutes* offset = nullptr);

    // [time.cal.ymdlast], class year_month_day_last
    class year_month_day_last;

    constexpr bool operator==(const year_month_day_last& x,
                              const year_month_day_last& y) noexcept;
    constexpr strong_ordering operator<=>(const year_month_day_last& x,
                                          const year_month_day_last& y) noexcept;

    constexpr year_month_day_last
      operator+(const year_month_day_last& ymdl, const months& dm) noexcept;
    constexpr year_month_day_last
      operator+(const months& dm, const year_month_day_last& ymdl) noexcept;
    constexpr year_month_day_last
      operator+(const year_month_day_last& ymdl, const years& dy) noexcept;
    constexpr year_month_day_last
      operator+(const years& dy, const year_month_day_last& ymdl) noexcept;
    constexpr year_month_day_last
      operator-(const year_month_day_last& ymdl, const months& dm) noexcept;
    constexpr year_month_day_last
      operator-(const year_month_day_last& ymdl, const years& dy) noexcept;

    template<class charT, class traits>
      basic_ostream<charT, traits>&
        operator<<(basic_ostream<charT, traits>& os, const year_month_day_last& ymdl);

    // [time.cal.ymwd], class year_month_weekday
    class year_month_weekday;

    constexpr bool operator==(const year_month_weekday& x,
                              const year_month_weekday& y) noexcept;

    constexpr year_month_weekday
      operator+(const year_month_weekday& ymwd, const months& dm) noexcept;
    constexpr year_month_weekday
      operator+(const months& dm, const year_month_weekday& ymwd) noexcept;
    constexpr year_month_weekday
      operator+(const year_month_weekday& ymwd, const years& dy) noexcept;
    constexpr year_month_weekday
      operator+(const years& dy, const year_month_weekday& ymwd) noexcept;
    constexpr year_month_weekday
      operator-(const year_month_weekday& ymwd, const months& dm) noexcept;
    constexpr year_month_weekday
      operator-(const year_month_weekday& ymwd, const years& dy) noexcept;

    template<class charT, class traits>
      basic_ostream<charT, traits>&
        operator<<(basic_ostream<charT, traits>& os, const year_month_weekday& ymwdi);

    // [time.cal.ymwdlast], class year_month_weekday_last
    class year_month_weekday_last;

    constexpr bool operator==(const year_month_weekday_last& x,
                              const year_month_weekday_last& y) noexcept;

    constexpr year_month_weekday_last
      operator+(const year_month_weekday_last& ymwdl, const months& dm) noexcept;
    constexpr year_month_weekday_last
      operator+(const months& dm, const year_month_weekday_last& ymwdl) noexcept;
    constexpr year_month_weekday_last
      operator+(const year_month_weekday_last& ymwdl, const years& dy) noexcept;
    constexpr year_month_weekday_last
      operator+(const years& dy, const year_month_weekday_last& ymwdl) noexcept;
    constexpr year_month_weekday_last
      operator-(const year_month_weekday_last& ymwdl, const months& dm) noexcept;
    constexpr year_month_weekday_last
      operator-(const year_month_weekday_last& ymwdl, const years& dy) noexcept;

    template<class charT, class traits>
      basic_ostream<charT, traits>&
        operator<<(basic_ostream<charT, traits>& os, const year_month_weekday_last& ymwdl);

    // [time.cal.operators], civil calendar conventional syntax operators
    constexpr year_month
      operator/(const year& y, const month& m) noexcept;
    constexpr year_month
      operator/(const year& y, int m) noexcept;
    constexpr month_day
      operator/(const month& m, const day& d) noexcept;
    constexpr month_day
      operator/(const month& m, int d) noexcept;
    constexpr month_day
      operator/(int m, const day& d) noexcept;
    constexpr month_day
      operator/(const day& d, const month& m) noexcept;
    constexpr month_day
      operator/(const day& d, int m) noexcept;
    constexpr month_day_last
      operator/(const month& m, last_spec) noexcept;
    constexpr month_day_last
      operator/(int m, last_spec) noexcept;
    constexpr month_day_last
      operator/(last_spec, const month& m) noexcept;
    constexpr month_day_last
      operator/(last_spec, int m) noexcept;
    constexpr month_weekday
      operator/(const month& m, const weekday_indexed& wdi) noexcept;
    constexpr month_weekday
      operator/(int m, const weekday_indexed& wdi) noexcept;
    constexpr month_weekday
      operator/(const weekday_indexed& wdi, const month& m) noexcept;
    constexpr month_weekday
      operator/(const weekday_indexed& wdi, int m) noexcept;
    constexpr month_weekday_last
      operator/(const month& m, const weekday_last& wdl) noexcept;
    constexpr month_weekday_last
      operator/(int m, const weekday_last& wdl) noexcept;
    constexpr month_weekday_last
      operator/(const weekday_last& wdl, const month& m) noexcept;
    constexpr month_weekday_last
      operator/(const weekday_last& wdl, int m) noexcept;
    constexpr year_month_day
      operator/(const year_month& ym, const day& d) noexcept;
    constexpr year_month_day
      operator/(const year_month& ym, int d) noexcept;
    constexpr year_month_day
      operator/(const year& y, const month_day& md) noexcept;
    constexpr year_month_day
      operator/(int y, const month_day& md) noexcept;
    constexpr year_month_day
      operator/(const month_day& md, const year& y) noexcept;
    constexpr year_month_day
      operator/(const month_day& md, int y) noexcept;
    constexpr year_month_day_last
      operator/(const year_month& ym, last_spec) noexcept;
    constexpr year_month_day_last
      operator/(const year& y, const month_day_last& mdl) noexcept;
    constexpr year_month_day_last
      operator/(int y, const month_day_last& mdl) noexcept;
    constexpr year_month_day_last
      operator/(const month_day_last& mdl, const year& y) noexcept;
    constexpr year_month_day_last
      operator/(const month_day_last& mdl, int y) noexcept;
    constexpr year_month_weekday
      operator/(const year_month& ym, const weekday_indexed& wdi) noexcept;
    constexpr year_month_weekday
      operator/(const year& y, const month_weekday& mwd) noexcept;
    constexpr year_month_weekday
      operator/(int y, const month_weekday& mwd) noexcept;
    constexpr year_month_weekday
      operator/(const month_weekday& mwd, const year& y) noexcept;
    constexpr year_month_weekday
      operator/(const month_weekday& mwd, int y) noexcept;
    constexpr year_month_weekday_last
      operator/(const year_month& ym, const weekday_last& wdl) noexcept;
    constexpr year_month_weekday_last
      operator/(const year& y, const month_weekday_last& mwdl) noexcept;
    constexpr year_month_weekday_last
      operator/(int y, const month_weekday_last& mwdl) noexcept;
    constexpr year_month_weekday_last
      operator/(const month_weekday_last& mwdl, const year& y) noexcept;
    constexpr year_month_weekday_last
      operator/(const month_weekday_last& mwdl, int y) noexcept;

    // [time.hms], class template hh_mm_ss
    template<class Duration> class hh_mm_ss;

    template<class charT, class traits, class Duration>
      basic_ostream<charT, traits>&
        operator<<(basic_ostream<charT, traits>& os, const hh_mm_ss<Duration>& hms);

    // [time.12], 12/24 hour functions
    constexpr bool is_am(const hours& h) noexcept;
    constexpr bool is_pm(const hours& h) noexcept;
    constexpr hours make12(const hours& h) noexcept;
    constexpr hours make24(const hours& h, bool is_pm) noexcept;

    // [time.zone.db], time zone database
    struct tzdb;
    class tzdb_list;

    // [time.zone.db.access], time zone database access
    const tzdb& get_tzdb();
    tzdb_list& get_tzdb_list();
    const time_zone* locate_zone(string_view tz_name);
    const time_zone* current_zone();

    // [time.zone.db.remote], remote time zone database support
    const tzdb& reload_tzdb();
    string remote_version();

    // [time.zone.exception], exception classes
    class nonexistent_local_time;
    class ambiguous_local_time;

    // [time.zone.info], information classes
    struct sys_info;
    template<class charT, class traits>
      basic_ostream<charT, traits>&
        operator<<(basic_ostream<charT, traits>& os, const sys_info& si);

    struct local_info;
    template<class charT, class traits>
      basic_ostream<charT, traits>&
        operator<<(basic_ostream<charT, traits>& os, const local_info& li);

    // [time.zone.timezone], class time_zone
    enum class choose {earliest, latest};
    class time_zone;

    bool operator==(const time_zone& x, const time_zone& y) noexcept;
    strong_ordering operator<=>(const time_zone& x, const time_zone& y) noexcept;

    // [time.zone.zonedtraits], class template zoned_traits
    template<class T> struct zoned_traits;

    // [time.zone.zonedtime], class template zoned_time
    template<class Duration, class TimeZonePtr = const time_zone*> class zoned_time;

    using zoned_seconds = zoned_time<seconds>;

    template<class Duration1, class Duration2, class TimeZonePtr>
      bool operator==(const zoned_time<Duration1, TimeZonePtr>& x,
                      const zoned_time<Duration2, TimeZonePtr>& y);

    template<class charT, class traits, class Duration, class TimeZonePtr>
      basic_ostream<charT, traits>&
        operator<<(basic_ostream<charT, traits>& os,
                   const zoned_time<Duration, TimeZonePtr>& t);

    // [time.zone.leap], leap second support
    class leap_second;

    bool operator==(const leap_second& x, const leap_second& y);
    strong_ordering operator<=>(const leap_second& x, const leap_second& y);

    template<class Duration>
      bool operator==(const leap_second& x, const sys_time<Duration>& y);
    template<class Duration>
      bool operator< (const leap_second& x, const sys_time<Duration>& y);
    template<class Duration>
      bool operator< (const sys_time<Duration>& x, const leap_second& y);
    template<class Duration>
      bool operator> (const leap_second& x, const sys_time<Duration>& y);
    template<class Duration>
      bool operator> (const sys_time<Duration>& x, const leap_second& y);
    template<class Duration>
      bool operator<=(const leap_second& x, const sys_time<Duration>& y);
    template<class Duration>
      bool operator<=(const sys_time<Duration>& x, const leap_second& y);
    template<class Duration>
      bool operator>=(const leap_second& x, const sys_time<Duration>& y);
    template<class Duration>
      bool operator>=(const sys_time<Duration>& x, const leap_second& y);
    template<class Duration>
      requires three_way_comparable_with<sys_seconds, sys_time<Duration>>
      constexpr auto operator<=>(const leap_second& x, const sys_time<Duration>& y);

    // [time.zone.link], class time_zone_link
    class time_zone_link;

    bool operator==(const time_zone_link& x, const time_zone_link& y);
    strong_ordering operator<=>(const time_zone_link& x, const time_zone_link& y);

    // [time.format], formatting
    template<class Duration> struct local-time-format-t;        // exposition only
    template<class Duration>
      local-time-format-t<Duration>
        local_time_format(local_time<Duration> time, const string* abbrev = nullptr,
                          const seconds* offset_sec = nullptr);
  }

  template<class Rep, class Period, class charT>
    struct formatter<chrono::duration<Rep, Period>, charT>;
  template<class Duration, class charT>
    struct formatter<chrono::sys_time<Duration>, charT>;
  template<class Duration, class charT>
    struct formatter<chrono::utc_time<Duration>, charT>;
  template<class Duration, class charT>
    struct formatter<chrono::tai_time<Duration>, charT>;
  template<class Duration, class charT>
    struct formatter<chrono::gps_time<Duration>, charT>;
  template<class Duration, class charT>
    struct formatter<chrono::file_time<Duration>, charT>;
  template<class Duration, class charT>
    struct formatter<chrono::local_time<Duration>, charT>;
  template<class Duration, class charT>
    struct formatter<chrono::local-time-format-t<Duration>, charT>;
  template<class charT> struct formatter<chrono::day, charT>;
  template<class charT> struct formatter<chrono::month, charT>;
  template<class charT> struct formatter<chrono::year, charT>;
  template<class charT> struct formatter<chrono::weekday, charT>;
  template<class charT> struct formatter<chrono::weekday_indexed, charT>;
  template<class charT> struct formatter<chrono::weekday_last, charT>;
  template<class charT> struct formatter<chrono::month_day, charT>;
  template<class charT> struct formatter<chrono::month_day_last, charT>;
  template<class charT> struct formatter<chrono::month_weekday, charT>;
  template<class charT> struct formatter<chrono::month_weekday_last, charT>;
  template<class charT> struct formatter<chrono::year_month, charT>;
  template<class charT> struct formatter<chrono::year_month_day, charT>;
  template<class charT> struct formatter<chrono::year_month_day_last, charT>;
  template<class charT> struct formatter<chrono::year_month_weekday, charT>;
  template<class charT> struct formatter<chrono::year_month_weekday_last, charT>;
  template<class Rep, class Period, class charT>
    struct formatter<chrono::hh_mm_ss<duration<Rep, Period>>, charT>;
  template<class charT> struct formatter<chrono::sys_info, charT>;
  template<class charT> struct formatter<chrono::local_info, charT>;
  template<class Duration, class TimeZonePtr, class charT>
    struct formatter<chrono::zoned_time<Duration, TimeZonePtr>, charT>;

  namespace chrono {
    // [time.parse], parsing
    template<class charT, class traits, class Alloc, class Parsable>
      unspecified
        parse(const basic_string<charT, traits, Alloc>& format, Parsable& tp);

    template<class charT, class traits, class Alloc, class Parsable>
      unspecified
        parse(const basic_string<charT, traits, Alloc>& format, Parsable& tp,
              basic_string<charT, traits, Alloc>& abbrev);

    template<class charT, class traits, class Alloc, class Parsable>
      unspecified
        parse(const basic_string<charT, traits, Alloc>& format, Parsable& tp,
              minutes& offset);

    template<class charT, class traits, class Alloc, class Parsable>
      unspecified
        parse(const basic_string<charT, traits, Alloc>& format, Parsable& tp,
              basic_string<charT, traits, Alloc>& abbrev, minutes& offset);

    // calendrical constants
    inline constexpr last_spec last{};

    inline constexpr weekday Sunday{0};
    inline constexpr weekday Monday{1};
    inline constexpr weekday Tuesday{2};
    inline constexpr weekday Wednesday{3};
    inline constexpr weekday Thursday{4};
    inline constexpr weekday Friday{5};
    inline constexpr weekday Saturday{6};

    inline constexpr month January{1};
    inline constexpr month February{2};
    inline constexpr month March{3};
    inline constexpr month April{4};
    inline constexpr month May{5};
    inline constexpr month June{6};
    inline constexpr month July{7};
    inline constexpr month August{8};
    inline constexpr month September{9};
    inline constexpr month October{10};
    inline constexpr month November{11};
    inline constexpr month December{12};
  }

  inline namespace literals {
  inline namespace chrono_literals {
    // [time.duration.literals], suffixes for duration literals
    constexpr chrono::hours                                 operator""h(unsigned long long);
    constexpr chrono::duration<unspecified, ratio<3600, 1>> operator""h(long double);

    constexpr chrono::minutes                             operator""min(unsigned long long);
    constexpr chrono::duration<unspecified, ratio<60, 1>> operator""min(long double);

    constexpr chrono::seconds               operator""s(unsigned long long);
    constexpr chrono::duration<unspecified> operator""s(long double);

    constexpr chrono::milliseconds                 operator""ms(unsigned long long);
    constexpr chrono::duration<unspecified, milli> operator""ms(long double);

    constexpr chrono::microseconds                 operator""us(unsigned long long);
    constexpr chrono::duration<unspecified, micro> operator""us(long double);

    constexpr chrono::nanoseconds                 operator""ns(unsigned long long);
    constexpr chrono::duration<unspecified, nano> operator""ns(long double);

    // [time.cal.day.nonmembers], non-member functions
    constexpr chrono::day  operator""d(unsigned long long d) noexcept;

    // [time.cal.year.nonmembers], non-member functions
    constexpr chrono::year operator""y(unsigned long long y) noexcept;
  }
  }

  namespace chrono {
    using namespace literals::chrono_literals;
  }
}
```
