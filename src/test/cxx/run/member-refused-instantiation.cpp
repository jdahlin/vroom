// [temp.inst]/2: a non-static data member whose class template specialization
// is valid works alongside sibling templates.
template <class T> struct Inner {
    T v;
};
struct Outer {
    Inner<int> a;
    Inner<long> b;
};

int main() {
    Outer o{{3}, {4}};
    return o.a.v + static_cast<int>(o.b.v) != 7;
}
