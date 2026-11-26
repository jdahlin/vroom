// [temp.variadic]: a pack expansion as the initializer of a scalar member in a
// constructor template.
struct S {
    int x;
    template <class... A> S(A... a) : x(a...) {}
};

int main() {
    S s(1);
    return s.x != 1;
}
