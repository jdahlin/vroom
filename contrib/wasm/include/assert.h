/* explorer stub */
#undef assert
#ifdef NDEBUG
#define assert(x) ((void)0)
#else
void __assert_fail(const char *, const char *, unsigned, const char *);
#define assert(x) ((void)((x) || (__assert_fail(#x, __FILE__, __LINE__, __func__), 0)))
#endif
#define static_assert _Static_assert
