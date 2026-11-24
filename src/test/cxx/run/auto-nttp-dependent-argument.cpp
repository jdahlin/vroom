// [temp.arg.nontype]/2, [dcl.type.auto.deduct]/2: a template <auto V> parameter
// named as the argument of a parameter of a fixed enum type; the type is known
// only at the instantiation.
enum class Tr { a, b };
template <class T, Tr Traits = Tr::a> struct holder {
    T *p;
    T *get() const { return p; }
    int trait() const { return static_cast<int>(Traits); }
};
template <typename T> static inline T *ref(T *obj) { return obj; }
template <typename T, auto Traits>
static inline T *ref(const holder<T, Traits> &h) {
    return ref(h.get());
}
template <typename T, auto Traits> int trait_of(const holder<T, Traits> &h) {
    return h.trait();
}
int main() {
    int x = 0;
    holder<int, Tr::b> h{&x};
    if (ref(h) != &x)
        return 1;
    if (trait_of(h) != 1)
        return 2;
    return 0;
}
