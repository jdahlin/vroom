/* [temp.pre]/9, [temp.names]/8: a class template whose template-head has
   a requires-clause, declared, then defined with an equivalent one
   ([temp.over.link]/6-7), named with arguments that satisfy it */
template <class T> constexpr bool is_small = sizeof(T) <= 4;

template <class T>
    requires is_small<T> && (sizeof(T) > 1)
struct box;

template <class T>
    requires is_small<T> && (sizeof(T) > 1)
struct box {
    T value;
    T twice() const { return value + value; }
};

int main()
{
    box<int> a{20};
    box<short> b{3};
    if (a.twice() != 40)
        return 1;
    if (b.twice() != 6)
        return 2;
    return sizeof(box<int>) == sizeof(int) ? 0 : 3;
}
