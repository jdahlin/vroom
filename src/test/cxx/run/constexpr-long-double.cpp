/* [expr.const]: a long double constant a double holds is evaluated: the
   constexpr limit of libc++'s sleep_for, comparisons and conversions. */
extern "C" int printf(const char *, ...);

struct duration {
    long double count;
    constexpr explicit duration(long double c) : count(c) {}
    constexpr bool less(const duration &o) const { return count < o.count; }
};

template <class T> int clamp(T d)
{
    constexpr duration max = duration(9223372036.0L);
    if (duration(d).less(max))
        return 1;
    return 0;
}

constexpr long double from_int = 7;
constexpr long double from_float = 1.5f;
constexpr long double negated = -from_float;
static_assert(from_int == 7.0L && from_float > 1.0L && negated < 0);
static_assert(static_cast<int>(from_int) == 7);

int main()
{
    static constexpr duration limit(9223372036.0L);
    long double runtime = from_int / 3;
    printf("%d %d %.1Lf %.1Lf %.1Lf %.6Lf %.1Lf\n", clamp(1.0L), clamp(1e12L),
           limit.count, from_float, negated, runtime, 0.5L + from_int);
    return 0;
}
