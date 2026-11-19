// A class derived from a non-copyable base whose default constructor is
// X() = default, as Skia's SkNoncopyable: the implicit copy constructor is
// deleted ([class.copy.ctor]/10.1), the default constructor still works.
extern "C" int printf(const char *, ...);

class Noncopyable {
public:
    Noncopyable() = default;

private:
    Noncopyable(const Noncopyable &) = delete;
    Noncopyable &operator=(const Noncopyable &) = delete;
};

class [[nodiscard]] Buffer : Noncopyable {
public:
    int size;
    int sum() const { return size * 2; }
};

struct Holder {
    Buffer buffer;
    int extra = 5;
};

int main()
{
    Buffer b;
    b.size = 20;
    Holder h;
    h.buffer.size = 1;
    printf("%d %d %d\n", b.sum(), h.buffer.sum(), h.extra);
    return b.sum() + h.extra == 45 ? 0 : 1;
}
