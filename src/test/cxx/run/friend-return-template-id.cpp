// [class.friend]/6, [temp.names]/1: a friend function definition whose return
// type is a simple-template-id (alias template or class template specialization)
// does not declare a class.
template <class T> using alias_t = T;

template <class T> struct Box {
    T v;
    friend constexpr alias_t<T> unwrap(const Box &b) { return b.v; }
};

int main() {
    Box<int> b{13};
    return unwrap(b) != 13;
}
