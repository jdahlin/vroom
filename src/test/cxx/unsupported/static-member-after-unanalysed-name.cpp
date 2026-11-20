/* [expr.const]/3, [class.static.data]/4: a static const member whose
   initializer names an enumerator that analysis cannot look up (the
   extern "C" block holding it is not parsed yet, and a namespace it
   defines is searched first) has an unknown value, as a variable at
   namespace scope; the next member's initializer that reads it is refused
   as unsupported, not reported as a non-constant expression. */
extern "C" {
namespace std {
inline namespace __1 {
namespace __math {
}
}
}
enum { E = 4 };
_Float16 z;
}
namespace std {
inline namespace __1 {
struct S {
    static const unsigned short a = E;
    static const int b = a;
    static const int c = a | 1;
};
}
}
