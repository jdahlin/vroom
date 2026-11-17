// [class.union]: a union's members at offset 0, its size the largest's;
// aggregate initialization of the first member ([dcl.init.aggr]/16);
// union in an elaborated-type-specifier; copies and passing by value
union U { int i; float f; char c[6]; };
union U g = {7};
static union U z;
int take(union U u) { return u.i; }
int main() {
    U u = {0x3f800000};
    if (u.f != 1.0f) return 1;
    if (sizeof(U) != 8 || alignof(U) != 4) return 2;
    u.c[0] = 5;
    union U v = u;
    if (take(v) != (0x3f800000 & ~0xff | 5)) return 3;
    if (g.i != 7 || z.i) return 4;
    return 0;
}
