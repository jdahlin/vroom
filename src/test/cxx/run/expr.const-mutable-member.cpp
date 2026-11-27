/* [expr.const]: a constexpr construction that initializes and reads a mutable
   member (SkMatrix); the object is writable at run time ([dcl.stc]/9) */
#include <stdint.h>
struct M { float f[9]; mutable int m;
  constexpr M() : M(1,0,0,0,1,0,0,0,1, 16) {}
  constexpr M(float a,float b,float c,float d,float e,float g,float h,float i,float j,int mask) : f{a,b,c,d,e,g,h,i,j}, m(mask) {}
};
const M &I() { static constexpr M identity; return identity; }
struct N { mutable int c; int d; constexpr N() : c(3), d(c + 1) { c += d; } };
static constexpr N gn;
static_assert(gn.d == 4);
constexpr int local() { N n; n.c++; return n.c; }
static_assert(local() == 8);
int main() {
    const M &m = I();
    m.m += 2;
    if (m.m != 18 || m.f[0] != 1 || m.f[4] != 1 || m.f[8] != 1 || m.f[1] != 0) return 1;
    static constexpr N n;
    gn.c++; n.c++;
    if (gn.c != 8 || n.c != 8 || n.d != 4) return 2;
    return 0;
}
