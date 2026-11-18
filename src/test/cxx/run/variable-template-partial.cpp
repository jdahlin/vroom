/* Partial specializations of variable templates ([temp.spec.partial]),
   libc++'s __is_referenceable_v: the void_t argument substituted when
   matching ([temp.class.spec.match]/3), a reference to void ill-formed
   ([dcl.ref]/1) and so no match */
template <class...> using void_t = void;
template <class T, class = void> inline const bool is_referenceable = false;
template <class T>
inline const bool is_referenceable<T, void_t<T &>> = true;

template <class T, class U> constexpr int same = 0;
template <class T> constexpr int same<T, T> = 1;
template <class T> constexpr int same<T *, T> = 2;

template <class T> int counter = 10;
template <class T> int counter<T *> = 20;

static_assert(is_referenceable<int> && !is_referenceable<void>, "");
static_assert(is_referenceable<int()> && is_referenceable<int &>, "");
static_assert(same<int, int> == 1 && same<int *, int> == 2 &&
                  same<int, long> == 0,
              "");

static const bool *address(const bool &b)
{
    return &b;
}
int main()
{
    if (!*address(is_referenceable<long>) || *address(is_referenceable<void>))
        return 1;
    counter<char *>++;
    if (counter<char *> != 21 || counter<char> != 10 || same<char, char> != 1)
        return 2;
    return 0;
}
