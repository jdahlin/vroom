// std: c++20
// [temp.constr.decl]/3, [dcl.fct.def.general]/1: trailing requires-clause on
// non-template member functions of a class template, and override/final on a
// defaulted virtual destructor.
struct Base {
    virtual ~Base() = default;
    virtual int id() const { return 1; }
};

struct Derived : Base {
    ~Derived() noexcept override = default;
    int id() const override { return 2; }
};

template <class T> struct Box {
    T v;
    int get() const requires (sizeof(T) >= 4) { return static_cast<int>(v); }
    int get() const requires (sizeof(T) < 4) { return static_cast<int>(v) + 10; }
};

int main() {
    Derived d;
    Box<int> bi{5};
    Box<char> bc{3};
    return d.id() != 2 || bi.get() != 5 || bc.get() != 13;
}
