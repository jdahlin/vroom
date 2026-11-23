/* cxx-run-flags: -std=c++20 */
/* [temp.class.spec.match]/2: a partial specialization matches only if the
   deduced arguments satisfy its requires-clause; [temp.class.spec]/9
   Example 1's #2: the primary's own arguments, more constrained than the
   primary (libc++'s common_reference.h); equal arguments with different
   constraints are not equivalent ([temp.over.link]/7) */
template <class T> concept Small = sizeof(T) <= 4;
template <class T> concept HasType = sizeof(typename T::type) > 0;
template <class T> struct has_type {
    static constexpr bool value = false;
};
template <class T>
    requires HasType<T>
struct has_type<T> {
    static constexpr bool value = true;
};

template <class T, class U> struct pick {
    static constexpr int value = 0;
};
template <class T, class U>
    requires Small<T> && Small<U>
struct pick<T *, U> {
    static constexpr int value = 1;
};
template <class T, class U>
    requires (sizeof(T) > 4)
struct pick<T *, U> {
    static constexpr int value = 2;
};

template <class T> struct ref {
    static constexpr int value = 0;
};
template <class T>
    requires Small<T>
struct ref<T &> {
    static constexpr int value = 1;
};

struct with_type {
    typedef int type;
};

static_assert(has_type<with_type>::value && !has_type<int>::value);
static_assert(pick<int *, char>::value == 1);
static_assert(pick<double *, char>::value == 2);
static_assert(pick<int *, double>::value == 0);
static_assert(pick<int, int>::value == 0);
static_assert(ref<int &>::value == 1 && ref<double &>::value == 0);

int main()
{
    return has_type<with_type>::value + pick<int *, char>::value +
           ref<int &>::value - 3;
}
