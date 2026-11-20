// [class.default.ctor]/1: a constructor template is a user-declared
// constructor ([temp.mem]/1), so a class with one and a defaulted move
// constructor has no implicit default constructor (libc++'s std::pair).
struct P {
    int v;
    template <class T> P(T t, int) : v(t) {}
    P(P &&) = default;
};

int main() {
    return sizeof(P) == sizeof(int) ? 0 : 1;
}
