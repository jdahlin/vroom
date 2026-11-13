typedef long long v1ll __attribute__((vector_size(8)));
struct ld { long double x; };
struct padded { float x __attribute__((aligned(16))); };
v1ll vector_identity(v1ll);
struct ld ld_identity(struct ld);
struct padded padded_sum(struct padded, int, struct padded);
int padded_va(int, ...);
struct odd { signed char a:3; short b __attribute__((packed)); };
struct odd odd_identity(struct odd);
#ifdef __x86_64__
typedef float v16f __attribute__((vector_size(64)));
v16f wide_identity(v16f);
#endif
typedef unsigned int underaligned __attribute__((aligned(2)));
struct under { char a; underaligned b; unsigned int c; };
struct under under_sum(struct under, int, struct under);
