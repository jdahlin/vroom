namespace ns {
template <class T>
struct rem_ref {
    using type = T;
};
template <class T>
struct rem_ref<T &> {
    using type = T;
};
template <class T>
struct rem_const {
    using type = T;
};
struct outer {
    struct inner {
        int v;
    };
};
using alias = outer;
} // namespace ns

using ns::alias;

int main() {
    typename ns::rem_const<typename ns::rem_ref<alias::inner &>::type>::type x{
        42};
    return x.v == 42 ? 0 : 1;
}
