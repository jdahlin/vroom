/* [temp.expl.spec]/1, [dcl.meaning]/1: a variable template's explicit
   specialization named by a qualified template-id, from the enclosing
   namespace (libc++'s __format::__enable_insertable) */
namespace N {
template <class T>
inline constexpr bool enabled = false;
template <class T>
constexpr int count = 1;
namespace M {
template <class T>
constexpr int deep = 2;
}
} // namespace N

template <>
inline constexpr bool N::enabled<char> = true;
template <>
constexpr int N::count<int> = 5;
template <>
constexpr int N::M::deep<long> = 9;

using namespace N;
using namespace N::M;

int main()
{
    int r = 0;
    if (!enabled<char>)
        r |= 1;
    if (enabled<int>)
        r |= 2;
    if (count<int> != 5)
        r |= 4;
    if (count<long> != 1)
        r |= 8;
    if (deep<long> != 9 || deep<int> != 2)
        r |= 16;
    return r;
}
