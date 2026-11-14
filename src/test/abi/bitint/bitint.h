typedef unsigned __int128 U;
#define DECL(N) \
 typedef _BitInt(N) S##N; \
 typedef unsigned _BitInt(N) U##N; \
 S##N signed##N(S##N); \
 U##N unsigned##N(U##N); \
 S##N callback##N(S##N); \
 S##N call##N(S##N); \
 U##N stack##N(long,long,long,long,long,U##N,long,long,long);
DECL(2) DECL(7) DECL(9) DECL(17) DECL(31) DECL(32) DECL(33)
DECL(63) DECL(64) DECL(65) DECL(96) DECL(127) DECL(128)
unsigned _BitInt(1) one(unsigned _BitInt(1));
U65 stackarm(long,long,long,long,long,long,long,U65,long);
U128 variadic(int,...);
struct R { unsigned char a; U65 b; S7 c; };
struct R record(struct R);
