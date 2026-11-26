/* [expr.type.conv]/1 with a qualified-id the parser cannot tell for a type: a
   member typedef or class of a specialization reached through another
   specialization's member (json::object_t::value_type(...)) */
template <class K> struct entry {
    K key;
    int value;
    entry(K k, int v) : key(k), value(v) {}
};
template <class K> struct table {
    typedef entry<K> value_type;
    typedef long size_type;
    struct node {
        int n;
        node(int x) : n(x + 1) {}
    };
};
template <class T = int> struct document {
    typedef table<T> object_t;
};
using doc = document<>;

int main()
{
    auto a = doc::object_t::value_type(1, 2);
    if (a.key != 1 || a.value != 2)
        return 1;
    auto b = doc::object_t::value_type(a);
    if (b.value != 2)
        return 2;
    if (doc::object_t::size_type(3.5) != 3)
        return 3;
    if (doc::object_t::size_type() != 0)
        return 4;
    if (doc::object_t::node(4).n != 5)
        return 5;
    return 0;
}
