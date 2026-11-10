#pragma GCC warning "hello"
#pragma GCC error "bad"
#define D defined(X)
#if D
#endif
#define E(x) defined x
#if E(Y)
#endif
#pragma GCC warning
int z;
