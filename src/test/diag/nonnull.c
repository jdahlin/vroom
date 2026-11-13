#define NULL ((void*)0)
__attribute__((nonnull)) void a(int *p, int *q);
__attribute__((nonnull(2))) void b(int *p, int *q);
void c(int *p __attribute__((nonnull)), int x);
void d(const char *f, ...) __attribute__((nonnull(1)));
void t(int *z) { a(0, z); a(z, NULL); b(0, z); b(z, 0); c(0, 1); d(0); a(z, (int*)0); a(z, 0L); }
