// [temp.deduct]/8 and [temp.deduct.call]/4: a call of the one template of a
// name whose deduction fails, in a requires-expression, is a substitution
// failure; a derived class argument still deduces its base.
template <class T> concept class_type = __is_class(T);
template <class D>
    requires class_type<D>
struct interface {};

template <class A, class B> concept same = __is_same(A, B);

template <class Op, class Y>
    requires(!same<Op, interface<Y>>)
void derived(interface<Y> *);

template <class T>
constexpr bool is_view = requires { derived<T>((T *)nullptr); };

struct V : interface<V> {};
struct W {};
template <class T> struct Vec {
    T *p;
};

int main()
{
    if (!is_view<V>)
        return 1;
    if (is_view<W> || is_view<Vec<int>>)
        return 2;
    return 0;
}
