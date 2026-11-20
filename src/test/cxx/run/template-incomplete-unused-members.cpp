// [temp.inst]/3, /11: taking sizeof instantiates the class layout only.
template <class T> void retain(T* p) { if (p) p->ref(); }
template <class T> void release(T* p) { if (p) p->unref(); }

template <class T> struct Ref {
    T* ptr;
    explicit Ref(T* p) : ptr(p) {}
    Ref(const Ref& other) : ptr(other.ptr) { retain(ptr); }
    ~Ref() { release(ptr); }
};

struct Forward;
using ForwardRef = Ref<Forward>;
static_assert(sizeof(ForwardRef) == sizeof(Forward*));
struct CompleteLater;
static_assert(sizeof(Ref<CompleteLater>) == sizeof(CompleteLater*));

struct CompleteLater {
    int refs = 1;
    void ref() { ++refs; }
    void unref() { --refs; }
};

int main() {
    CompleteLater object;
    {
        Ref<CompleteLater> first(&object);
        Ref<CompleteLater> second(first);
        if (object.refs != 2)
            return 1;
    }
    return object.refs;
}
