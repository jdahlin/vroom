/* [over.ics.list]/4, /10: list-initialize a class parameter from a
   multi-element list in an overload set. */
template<class T, int Extent> struct Span {
    T *ptr;
    int size;
    Span(T *p, int n) : ptr(p), size(n) {}
};

struct Size {
    int width;
    int height;
};

static int make(Size size, int color, int alpha, void *) {
    return size.width * size.height + color + alpha;
}

static int map(Span<int, 2> span) {
    return span.size + (span.ptr[0] + span.ptr[1]);
}

static int map(int n) { return n; }
static int area(int n) { return n; }

static int area(Size size) { return size.width * size.height; }

int main()
{
    int values[] = {3, 5};
    return map({values, 2}) == 10 && area({3, 5}) == 15 &&
                   make({3, 5}, 7, 9, nullptr) == 31
               ? 0
               : 1;
}
