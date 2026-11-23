/* Class templates of namespace scope with type parameters: members of a
   parameter's type, member functions using it, constructors, the
   injected-class-name, a specialization as another's argument, a
   template of a namespace named with its qualifier, a function template
   over a class template's specializations ([temp.class], [temp.names],
   [temp.inst], [temp.local]). */
#include <stdio.h>

template <class T> struct box {
    T value;
    T get() const { return value; }
    void set(T v) { value = v; }
    box with(T v) const
    {
        box copy = *this;
        copy.value = v;
        return copy;
    }
};

template <class K, class V> struct pair {
    K key;
    V value;
    pair(K k, V v) : key(k), value(v) {}
    int key_size() const { return sizeof(K); }
};

template <class T> struct node {
    T item;
    node<T> *next;
    int length() const
    {
        int n = 1;
        for (const node *p = next; p; p = p->next)
            n++;
        return n;
    }
};

namespace geometry {
template <class T> struct point {
    T x, y;
    T dot(point other) const { return x * other.x + y * other.y; }
};
} // namespace geometry

template <class T> T unbox(box<T> b) { return b.get(); }

int main()
{
    int failures = 0;
    box<int> b;
    b.set(3);
    box<double> d = {1.5};
    if (b.get() != 3 || d.get() != 1.5 || sizeof(box<char>) != 1)
        failures |= 1;
    if (b.with(9).get() != 9 || b.get() != 3)
        failures |= 2;
    pair<char, long> p('k', 7);
    if (p.key != 'k' || p.value != 7 || p.key_size() != 1 ||
        sizeof(pair<char, long>) != 16)
        failures |= 4;
    node<int> c = {3, 0};
    node<int> a = {1, &c};
    if (a.length() != 2 || a.next->item != 3)
        failures |= 8;
    geometry::point<int> u = {1, 2}, v = {3, 4};
    if (u.dot(v) != 11)
        failures |= 16;
    box<box<int>> nested = {{5}};
    if (nested.get().get() != 5 || unbox(b) != 3 || unbox(d) != 1.5)
        failures |= 32;
    printf("%d\n", failures);
    return failures;
}
