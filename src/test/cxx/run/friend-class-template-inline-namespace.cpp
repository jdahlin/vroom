// [temp.friend]/3, [namespace.def]/6: `template<class> friend class
// ::n::jp;` names a class template declared in an inline namespace of n;
// every specialization of it is a friend.
namespace n {
inline namespace v1 {
template <typename S> class jp;
template <typename S = int> class bj {
    template <typename> friend class ::n::jp;
    int x = 4;
};
template <typename S> class jp {
public:
    int get(bj<S> &b) { return b.x; }
};
} // namespace v1
} // namespace n
int main()
{
    n::bj<> b;
    n::jp<int> p;
    if (p.get(b) != 4)
        return 1;
    return 0;
}
