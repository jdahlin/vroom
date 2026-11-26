// [temp.class.spec]/6: namespace-qualified partial specialization
// (e.g. template <typename T> struct std::hash<Box<T>>).
namespace std {
template <typename T>
struct hash {
    int operator()(const T &) const { return 0; }
};
} // namespace std

template <typename T>
struct Box {
    T x;
};

template <typename T>
struct std::hash<Box<T>> {
    int operator()(const Box<T> &b) const { return b.x; }
};

int main()
{
    std::hash<Box<int>> h;
    return h(Box<int>{42}) == 42 ? 0 : 1;
}
