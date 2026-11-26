// [conv.ptr]/3: the conversion to a virtual base of a reference a call
// returns reads it after the call (std::getline(...) as a condition).
extern "C" int puts(const char *);
struct ios { int st = 0; virtual ~ios() {} bool fail() const { return st & 5; } explicit operator bool() const { return !fail(); } };
template <class C> struct bios : ios {};
template <class C> struct is : virtual bios<C> { int g = 1; is &read(int &v) { v = 1; return *this; } };
template <class C> struct ss : is<C> { int s = 2; };
template <class C> is<C> &getl(is<C> &i) { return i; }
int main() {
    ss<char> x; int v = 0;
    if (x) puts("ok1");
    if (getl(x)) puts("ok2");
    if (x.read(v)) puts("ok3");
    if (!(x.read(v))) return 1; else puts("ok4");
    return 0;
}
