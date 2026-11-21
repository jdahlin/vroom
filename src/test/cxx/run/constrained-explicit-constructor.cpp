// std: c++20
// [temp.constr.decl]/3, [dcl.fct.spec]/2: non-template constructors of a
// class template with a trailing requires-clause and explicit(bool).
template <class T, bool Explicit = false>
struct SpanLike {
    T *ptr;
    constexpr SpanLike() : ptr(nullptr) {}
    constexpr explicit(Explicit) SpanLike(T *p)
        requires (sizeof(T) >= 4)
        : ptr(p) {}
    constexpr SpanLike(T *p, int)
        requires (sizeof(T) < 4)
        : ptr(p) {}
};

SpanLike<int, false> make_implicit(int *p) {
    return p;
}

int main() {
    int x = 42;
    char c = 'a';
    SpanLike<int> a = make_implicit(&x);
    SpanLike<int, true> b(&x);
    SpanLike<char> s(&c, 1);
    return *a.ptr != 42 || *b.ptr != 42 || *s.ptr != 'a';
}
