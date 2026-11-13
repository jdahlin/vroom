#define VA(fmt, ...) p(fmt, ## __VA_ARGS__)
#define V0(...) p(x, ## __VA_ARGS__)
#define VN(fmt, args...) p(fmt, ## args)
#define E
#define V2(a, b, ...) p(a, b, ## __VA_ARGS__)
1: VA("x") VA("x",) VA("x", E) VA("x", 1)
2: V0() V0(E) V0(1)
3: VN(1) VN(1,) VN(1,E)
4: V2(1,2) V2(1,2,)
