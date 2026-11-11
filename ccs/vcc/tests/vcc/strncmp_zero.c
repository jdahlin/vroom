/* Zero-bound builtin calls still evaluate both string arguments and the bound.
   Also run with -DEXPECT_LIBCALL and -fno-builtin or -fno-builtin-strncmp. */
extern void abort(void);
extern int strncmp(const char *, const char *, __SIZE_TYPE__);
static int left, right, bound, library_calls;
__attribute__((noinline)) int strncmp(const char *a, const char *b, __SIZE_TYPE__ n) {
 library_calls++;
 while (n--) {
  unsigned char x = *a++, y = *b++;
  if (!x || x != y) return x - y;
 }
 return 0;
}
static const char *l(void) { left++; return "left"; }
static const char *r(void) { right++; return "right"; }
static int z(void) { bound++; return 0; }
int main(void) {
 if (strncmp(l(), r(), 0) != 0 || left != 1 || right != 1) abort();
 if (__builtin_strncmp(l(), r(), 0) != 0 || left != 2 || right != 2) abort();
#ifdef EXPECT_LIBCALL
 if (library_calls != 2) abort();
#endif
 if (strncmp(l(), r(), (bound++, 0)) != 0 || left != 3 || right != 3 || bound != 1) abort();
 if (__builtin_strncmp(l(), r(), (bound++, 0)) != 0 || left != 4 || right != 4 || bound != 2) abort();
 if (__builtin_strncmp(l(), r(), z()) != 0 || left != 5 || right != 5 || bound != 3) abort();
 if (strncmp("a", "b", 1) >= 0 || __builtin_strncmp("b", "a", 1) <= 0) abort();
 return 0;
}
