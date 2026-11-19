// [temp.dep.type]/9, [temp.alias]/2: an alias template used with an
// enclosing template's parameters (a partial specialization's) names the
// dependent template-id of a pack, X<K>::value..., as a nested-name-specifier;
// it is analysed again per specialization (libc++'s __enable_hash_helper).
template <class K> struct Has { static const bool value = true; };
template <bool...> struct all { static const bool value = true; };
template <bool B, class T = void> struct ei { typedef T type; };
template <class T, class> using imp = T;
template <class T, class... K>
using A = imp<T, typename ei<all<Has<K>::value...>::value>::type>;
template <class T> struct opt {};
template <class T> struct hash { static int f() { return 1; } };
template <class T> struct hash<A<opt<T>, T>> { static int f() { return 2; } };

int main()
{
    if (hash<opt<int>>::f() != 2)
        return 1;
    if (hash<int>::f() != 1)
        return 2;
    return 0;
}
