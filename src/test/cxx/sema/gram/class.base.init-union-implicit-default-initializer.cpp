// [class.base.init]/9.1-9.2: the implicit default constructor of a union
// initializes the variant member with a default member initializer and no
// other one
struct nt {
    constexpr nt() : x(100) {}
    int x;
};
union U2 {
    int a{1000};
    nt b;
};
static_assert(U2().a == 1000, "");
