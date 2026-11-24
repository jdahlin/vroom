/* [temp.friend]/1: a friend declaration naming a function template
   specialization may be constexpr; it is no explicit instantiation
   ([temp.explicit]/3) */
template <class T> struct box;
template <class T> constexpr box<T> make_box(T v);

template <class T> struct box {
    T v;
    friend constexpr box make_box<>(T v);
};

template <class T> constexpr box<T> make_box(T v)
{
    return box<T>{v};
}

int main(int argc, char **)
{
    box<int> b = make_box<int>(argc + 6);
    if (b.v != argc + 6)
        return 1;
    return 0;
}
