// [dcl.init.list]/3.8: direct-list-initialization of an enumeration with a
// fixed underlying type from a single element.
enum class E : unsigned char { A = 1, B = 2 };

template <typename Tenum, Tenum End = Tenum{5}>
constexpr unsigned char get_end()
{
    return static_cast<unsigned char>(End);
}

int main()
{
    E e{3};
    E f{e};
    return (static_cast<unsigned char>(e) == 3 &&
            static_cast<unsigned char>(f) == 3 && get_end<E>() == 5)
               ? 0
               : 1;
}
