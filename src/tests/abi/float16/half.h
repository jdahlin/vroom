#if defined(__aarch64__) || defined(__x86_64__)
typedef _Float16 half;
struct h4 { half a, b, c, d; };
half half_sum(half, half);
half half_many(half,half,half,half,half,half,half,half,half,half);
struct h4 half_record(struct h4, half);
half half_callback(half (*)(half, half), half);
half half_varargs(int, ...);
#endif
