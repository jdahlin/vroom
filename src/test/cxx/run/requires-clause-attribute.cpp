/* A requires-clause followed by an attribute-specifier-seq ([temp.pre]/9):
   [[ starts the declaration's attributes, not a subscript (libc++'s
   iter_move.h). */
template <class T>
concept small = sizeof(T) <= 4;

struct fn {
    template <class T>
        requires small<T>
    [[nodiscard]] int operator()(T x) const
    {
        return (int)x + 1;
    }
};

int main()
{
    fn f;
    return f(41) == 42 ? 0 : 1;
}
