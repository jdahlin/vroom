/* [temp.alias]/2, [temp.dep.type]: a specialization of an alias template
   is dependent by its arguments, not by the alias template's own name: a
   typedef-name of a class template specialization whose argument is
   EL<int> is no dependent type, and its members are found through it
   (vcpkg-tool: using MapT = std::map<K, ExpectedL<V>>; MapT::iterator). */
template <class T> struct E {
    struct iterator {
        int v;
    };
};
template <class T> using EL = E<T>;
template <class K, class V> struct M {
    struct iterator {
        int v;
    };
};
struct P {
    using MapT = M<int, EL<int>>;
    MapT::iterator next(MapT::iterator hint) { return {hint.v + 1}; }
};
int main() { return P().next({1}).v - 2; }
