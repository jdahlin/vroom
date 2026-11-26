// [class.copy.ctor]: a constructor from another specialization is chosen; the
// implicit members that a non-copyable member deletes are not needed.
struct NonCopy {
    NonCopy() = default;
    NonCopy(const NonCopy &) = delete;
};
int converted;
template <int N> struct T {
    NonCopy nc;
    T() = default;
    explicit T(const T<0> &) { converted++; }
};
void f(T<0> &a) { T<1> b(a); }

int main() {
    T<0> a;
    f(a);
    return converted != 1;
}
