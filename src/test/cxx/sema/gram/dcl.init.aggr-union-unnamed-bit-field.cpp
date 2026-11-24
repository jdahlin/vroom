// [dcl.init.aggr]/5.2, [dcl.init]/6: a union's first member for
// initialization and zero-initialization is the first named one
union UnionWithUnnamedBitfield {
    int : 3;
    int n;
};
static_assert(UnionWithUnnamedBitfield().n == 0, "");
static_assert(UnionWithUnnamedBitfield{}.n == 0, "");
static_assert(UnionWithUnnamedBitfield{1}.n == 1, "");
