/* [abi:mangling-compression]: substitutions and ::std's abbreviations */
namespace std
{
template <class T> struct allocator {};
template <class C> struct char_traits {};
template <class C, class T = char_traits<C>, class A = allocator<C>>
struct basic_string {};
template <class C, class T = char_traits<C>> struct basic_istream {};
template <class C, class T = char_traits<C>> struct basic_ostream {};
template <class C, class T = char_traits<C>> struct basic_iostream {};
template <class T> struct vector {};
} // namespace std
struct A {};
void repeated(A, A *, const A *, A &) {}
void pointers(int *, int *, int **) {}
namespace ns
{
struct B {};
void in_ns(B, B) {}
} // namespace ns
void strings(std::basic_string<char>, std::basic_istream<char>,
             std::basic_ostream<char>, std::basic_iostream<char>)
{
}
void other_string(std::basic_string<wchar_t>) {}
void allocators(std::allocator<int>, std::vector<int>) {}
// MANGLE: _Z8repeated1APS_PKS_RS_
// MANGLE: _Z8pointersPiS_PS_
// MANGLE: _ZN2ns5in_nsENS_1BES0_
// MANGLE: _Z7stringsSsSiSoSd
// MANGLE: _Z12other_stringSbIwSt11char_traitsIwESaIwEE
// MANGLE: _Z10allocatorsSaIiESt6vectorIiE
