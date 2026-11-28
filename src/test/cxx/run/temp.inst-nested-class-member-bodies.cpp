/* [temp.inst]/4: the member functions of a class template's nested class are
   instantiated where used, so they may need a template argument to be
   complete that was not when the specialization was */
template <class T> struct vec {
    T *first, *last;
    struct cursor {
        T *p;
        long n;
        cursor(vec &v) : p(v.first + 1), n(v.last - p) {}
        long span() const { return (p + n) - p; }
    };
    long second() { return cursor(*this).span(); }
};
struct item;
struct holder {
    vec<item> items;
};
static_assert(sizeof(vec<item>) == 2 * sizeof(void *));
struct item {
    long v[3];
};
int main()
{
    item data[4] = {};
    holder h{{data, data + 4}};
    return h.items.second() != 3;
}
