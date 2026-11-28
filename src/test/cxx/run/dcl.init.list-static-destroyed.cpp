/* [dcl.init.list]/6, [class.temporary]/6: the backing array of a static
   std::initializer_list lives as long as the list, and its elements with
   destructors are destroyed at exit ([basic.start.term]). */
extern "C" int printf(const char *, ...);

namespace std {
typedef decltype(sizeof 0) size_t;
template <class E>
class initializer_list {
    const E *array;
    size_t count;

public:
    initializer_list() : array(nullptr), count(0) {}
    const E *begin() const { return array; }
    const E *end() const { return array + count; }
    size_t size() const { return count; }
};
} // namespace std

struct Name {
    int id;
    Name(int i) : id(i) { printf("+%d\n", id); }
    Name(const Name &o) : id(o.id + 100) {}
    ~Name() { printf("-%d\n", id); }
};

static const std::initializer_list<Name> &local()
{
    static std::initializer_list<Name> once = {6, 7};
    return once;
}

int main()
{
    for (const Name &n : local())
        printf("%d\n", n.id);
    for (const Name &n : local())
        printf("%d\n", n.id);
    return 0;
}
