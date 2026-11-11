#if defined(__aarch64__) || defined(__x86_64__)
typedef __bf16 bfloat;
struct h4 { bfloat a, b, c, d; };
bfloat bfloat_sum(bfloat, bfloat);
bfloat bfloat_many(bfloat,bfloat,bfloat,bfloat,bfloat,bfloat,bfloat,bfloat,bfloat,bfloat);
struct h4 bfloat_record(struct h4, bfloat);
bfloat bfloat_callback(bfloat (*)(bfloat, bfloat), bfloat);
bfloat bfloat_varargs(int, ...);
#endif
#if defined(__aarch64__) || defined(__x86_64__)
struct mixed_half { bfloat a; _Float16 b; bfloat c; };
struct mixed_half bfloat_mixed(struct mixed_half);
#endif
