/* [basic.start.static]/2: constexpr variables hold their values before any
   dynamic initialization, a copy of a template's static member too
   (OpenTTD's window tables) */
/* cxx-run-symbols-differ: clang's are constant data */
namespace std
{
template <class E>
struct initializer_list {
    const E *first;
    decltype(sizeof 0) count;
};
}
struct Part {
    int a;
};
struct Desc {
    unsigned long n;
    int first;
    Desc(std::initializer_list<Part> s)
        : n(s.count)
        , first(s.first[0].a)
    {
    }
};
template <int C>
struct W {
    static constexpr auto parts = {Part{C}, Part{2}, Part{3}};
};
static constexpr auto w1 = W<7>::parts;
static Desc descs[] = {{w1}};
int main()
{
    return (int)descs[0].n + descs[0].first - 10;
}
