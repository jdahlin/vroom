/* [basic.scope.param]/1, [basic.scope.class]/1: the parameters of a member
   declared outside its class are in the class's scope, and so is its
   trailing-return-type; after the declarator the parser is back in the
   scope it was in, not in the class (a namespace reopened next is the
   namespace, not a new one inside the class: vcpkg-tool's span.h after
   fmt). */
namespace N {
typedef int T;
}
struct A {
    auto f(int) -> int;
};
auto A::f(int x) -> int { return x; }
namespace N {
typedef int U;
}
N::T a = 1;
N::U b = 2;
int main() { return A().f(a) + b - 3; }
