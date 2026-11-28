/* [temp.dep.type]/1: a typedef-name of the current instantiation names it;
   [temp.names]/2: a member template named through it is found, with no
   keyword template (OpenTTD's Pool::PoolItem::Iterate). */
template <class Titem, int N>
struct Pool {
    template <class T> struct Wrapper {
        int from;
        Wrapper(int f = 0) : from(f + N) {}
    };

    template <int M>
    struct Item {
        typedef struct Pool<Titem, N> Pool;
        static Pool::Wrapper<Titem> Iterate(int from)
        {
            return Pool::Wrapper<Titem>(from + M);
        }
        static int declared(int from)
        {
            Pool::Wrapper<Titem> w(from + M);
            return w.from;
        }
    };
};

struct X : Pool<X, 100>::Item<10> {};

int main()
{
    if (X::Iterate(3).from != 113)
        return 1;
    if (X::declared(4) != 114)
        return 2;
    return 0;
}
