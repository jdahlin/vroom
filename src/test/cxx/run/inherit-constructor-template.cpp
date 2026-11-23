// [namespace.udecl]/16: a class inherits the constructors of a base that
// has constructor templates, also through a member typedef of the base.
struct Base {
    int v;
    template <class T> Base(T x) : v(static_cast<int>(x)) {}
};
struct Derived : Base {
    using B = Base;
    using B::B;
};
struct Plain : Base {
    using Base::Base;
};

static_assert(sizeof(Derived) == sizeof(int), "");
static_assert(sizeof(Plain) == sizeof(int), "");

int main() { return 0; }
