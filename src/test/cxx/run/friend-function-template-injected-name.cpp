// [temp.friend]/2, [class.friend]/7: a friend function template defined
// inside a class template specialization can name the enclosing class's
// injected-class-name and template parameters in its declaration.
template <class T> struct Box {
    T v;
    template <class U>
    friend constexpr int combine(const Box &a, const Box<U> &b) {
        return static_cast<int>(a.v) + static_cast<int>(b.v);
    }
};

int main() {
    Box<int> a{5};
    Box<long> b{8};
    return combine(a, b) != 13;
}
