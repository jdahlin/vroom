/* [class.base.init]/3: a mem-initializer-id may be an alias for a base. */
template<class T, int Extent> struct Storage {
    T *ptr;
    int size;
    explicit Storage(T *p, int n) : ptr(p), size(n) {}
};

namespace example {
using ExternalBase = Storage<int, 1>;

struct ExternalSpan : ExternalBase {
    ExternalSpan(int *p) : ExternalBase{p, 1} {}
};
}

template<class T, int Extent> struct Span : Storage<T, Extent> {
    template<class Integer>
    Span(T *p, Integer size) : Inherited{p, size} {}

private:
    using Inherited = Storage<T, Extent>;
};

int main()
{
    int values[] = {3, 5};
    Span<int, 2> span(values, 2);
    example::ExternalSpan external(values + 1);
    return span.ptr != values || span.size != 2 ||
           external.ptr != values + 1 || external.size != 1;
}
