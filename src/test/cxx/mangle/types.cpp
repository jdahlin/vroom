void width(_Float16 *, __fp16 *, _Float16 *) {}
void bound(int (&)[]) {}
void bound(int (&)[0]) {}
void bound(int (&)[42]) {}
void wide_bound(char (&)[4294967297ULL]) {}
#if defined(__aarch64__) || defined(__x86_64__)
void bfloat(__bf16 *, __bf16 *) {}
#endif
