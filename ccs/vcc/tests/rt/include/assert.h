#undef assert
#ifdef NDEBUG
#define assert(e) ((void)0)
#else
void __rt_assert(const char *, const char *, int, const char *) __attribute__((noreturn));
#define assert(e) ((e) ? (void)0 : __rt_assert(#e, __FILE__, __LINE__, __func__))
#endif
