// [dcl.attr.grammar], [temp.class]/1: class template with an attribute
// without effect (e.g. [[clang::trivial_abi]]) in its class-head.
template <typename T>
class [[clang::trivial_abi]] Handle {
    T *ptr;
public:
    explicit Handle(T *p) : ptr(p) {}
    T get() const { return *ptr; }
};

int main() {
    int x = 23;
    Handle<int> h(&x);
    return h.get() != 23;
}
