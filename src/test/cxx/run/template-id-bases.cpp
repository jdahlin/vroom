/* [class.derived]/2: a base named by a simple-template-id, a class
   template's specialization ([temp.names]/1): of a non-template class, of
   a class template (its parameters bound in each specialization), and
   the curiously recurring form whose argument is the derived class.
   [temp.variadic]/5.3: a base-specifier followed by an ellipsis is one
   base per element of the pack, in order */
template <class T> struct holder {
    T value;
    T get() const { return value; }
};
struct ints : holder<int> {
    int extra;
};
template <class T> struct twice : holder<T> {
    T second;
};
template <class D> struct counted {
    int count() const { return static_cast<const D *>(this)->n; }
};
struct three : counted<three> {
    int n = 3;
};
template <class... T> struct listed : public holder<T>... {};
template <class... T> struct all : holder<T>... {};

static_assert(sizeof(ints) == 8 && sizeof(twice<short>) == 4);
static_assert(sizeof(listed<char, short, int>) == 8 && sizeof(listed<>) == 1);
static_assert(sizeof(all<char, int>) == 8);

int main()
{
    ints a;
    a.value = 4;
    a.extra = 5;
    twice<long> t;
    t.second = 6;
    holder<long> &h = t;
    h.value = 7;
    three r;
    all<char, int> m;
    holder<int> &mi = m;
    holder<char> &mc = m;
    mi.value = 8;
    mc.value = 9;
    if (a.get() != 4 || t.get() != 7 || t.second != 6 || r.count() != 3)
        return 1;
    if (reinterpret_cast<char *>(&mi) - reinterpret_cast<char *>(&m) != 4 ||
        mc.value != 9)
        return 2;
    return 0;
}
