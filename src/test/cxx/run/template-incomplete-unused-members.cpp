// [temp.inst]/3, /11: taking sizeof instantiates the class layout only.
template <class T> void retain(T* p) { if (p) p->ref(); }
template <class T> void release(T* p) { if (p) p->unref(); }

template <class T> struct Ref {
    T* ptr;
    Ref(const Ref& other) : ptr(other.ptr) { retain(ptr); }
    ~Ref() { release(ptr); }
};

struct Forward;
using ForwardRef = Ref<Forward>;
static_assert(sizeof(ForwardRef) == sizeof(Forward*));
int main() { return 0; }
