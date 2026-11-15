// typedef struct { ... } name;: the unnamed class is mangled by the typedef's name ([dcl.typedef]/9: glibc's
// __mbstate_t in std::fpos, std::streampos); a class's one conversion function to an arithmetic type converts to
// another one (static_cast<size_t>(pos) of a streampos: operator streamoff)
typedef struct { int a; } anon_t;
template <class S> struct P { S st; long long off; operator long long() const { return off; } };
unsigned long f(P<anon_t> p) { return static_cast<unsigned long>(p); }
int main() { P<anon_t> p{{1}, 7}; return f(p) == 7 ? 0 : 1; }
