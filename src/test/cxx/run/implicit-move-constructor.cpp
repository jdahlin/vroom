// [class.copy.ctor]/8, /14: the implicitly-declared move constructor of a
// class whose members have move constructors (as source_file's owned_array
// members): non-trivial, moves each base and member in order; the implicit
// copy constructor is deleted ([class.copy.ctor]/10.1) because the members
// cannot be copied.
extern "C" int printf(const char *, ...);
extern "C" void free(void *);

static int destroyed, moved;

class Array {
public:
    Array() = default;
    ~Array()
    {
        destroyed++;
        free(data);
    }
    Array(const Array &) = delete;
    Array &operator=(const Array &) = delete;
    Array(Array &&other) noexcept : data(other.data), count(other.count)
    {
        other.data = nullptr;
        other.count = 0;
        moved++;
    }
    void *data = nullptr;
    int count = 0;
};

struct Plain {
    int a = 1;
};

// a copy-only member is copied by the move constructor
struct Copyable {
    int v;
    Copyable() : v(9) {}
    Copyable(const Copyable &o) : v(o.v + 1) {}
};

struct File {
    const char *name = nullptr;
    int size = 0;
    int get() const { return size; }

private:
    Array first;
    Array second;
    Copyable c;
    Plain p;
};

struct Derived : File {
    int z = 0;
};

struct Holder {
    File f;
    Array a;
};

// reference members are bound again by the copy and the move
// ([class.copy.ctor]/14), as diagnostic_consumer's const diagnostic_options &
struct Options {
    int level = 4;
};

class Consumer {
public:
    explicit Consumer(const Options &options) : options(options) {}
    const Options &options;
    Array messages;
    int errors = 0;
};

// a copy (not a move) with a reference member
struct Bound {
    Copyable c;
    const Options &o;
    int &n;
    Bound(const Options &o, int &n) : o(o), n(n) {}
};

int main()
{
    File f;
    f.size = 5;
    File g(static_cast<File &&>(f));
    Derived d;
    d.size = 2;
    d.z = 3;
    Derived e(static_cast<Derived &&>(d));
    Holder h;
    h.f.size = 7;
    Holder i(static_cast<Holder &&>(h));
    Options o;
    o.level = 11;
    Consumer k(o);
    k.errors = 2;
    Consumer l(static_cast<Consumer &&>(k));
    int count = 6;
    Bound b(o, count);
    Bound b2(b);
    b2.n++;
    printf("%d %d %d %d\n", b2.c.v, b2.o.level, count, &b2.o == &o);
    printf("%d %d %d\n", l.options.level, l.errors, &l.options == &o);
    printf("%d %d %d %d %d %d\n", g.size, e.size, e.z, i.f.size, moved,
           destroyed);
    return 0;
}
