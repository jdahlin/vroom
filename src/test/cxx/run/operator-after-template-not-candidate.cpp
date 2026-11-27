// [temp.dep.candidate], [over.match.oper]/3.2: an operator declared after a
// template in an associated namespace is no candidate of operands none of
// which has a class or enumeration type (a comparison of pointers).
namespace n
{
template <typename T> struct Box {
    T v;
    bool self(const Box *that) const { return this == that; }
};
struct Tag {
};
enum E { e0, e1 };
template <typename T> bool pointers(T *a, T *b) { return a != b; }
inline bool operator==(Tag, Tag) { return false; }
inline bool operator!=(Tag, Tag) { return true; }
} // namespace n

int main()
{
    n::Box<n::Tag> a, b;
    int r = 0;
    if (a.self(&a))
        r += 1;
    if (!a.self(&b))
        r += 2;
    if (n::pointers(&a, &b))
        r += 4;
    return r - 7;
}
