// a class to an arithmetic type its one conversion function's type converts to (libc++'s fpos to off_t: fseeko)
struct P { long long off; operator long long() const { return off; } };
long take(long v) { return v; }
double dtake(double v) { return v; }
int main() { P p; p.off = 7; long x = p; return take(p) == 7 && x == 7 && dtake(p) == 7.0 ? 0 : 1; }
